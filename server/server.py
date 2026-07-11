"""
🚁 FALCON — Unified Drone Command Server
==========================================
Python Flask server that bridges:
  - ESP32 Drone Controller (flight controls)
  - ESP32-CAM (camera feed)
  - Ollama Local AI (image analysis)
All on the same SkyHund WiFi network.
"""

import base64
import io
import time
import requests
from flask import Flask, render_template, jsonify, request, Response
from flask_cors import CORS
from PIL import Image

# ============================================
# Configuration
# ============================================
OLLAMA_URL = "http://localhost:11434/api/chat"
OLLAMA_MODEL = "gemma3" # Or "gemma:2b", "gemma2:2b"

# ESP32 Drone Controller - AP is always 192.168.4.1
DRONE_IP = "192.168.4.1"
DRONE_DATA_URL = f"http://{DRONE_IP}/data"
DRONE_CONTROL_URL = f"http://{DRONE_IP}/control"

# ESP32-CAM - Station mode, IP assigned by drone AP (usually 192.168.4.2)
CAM_IP = "192.168.4.2"
CAM_STREAM_URL = f"http://{CAM_IP}:81/stream"
CAM_CAPTURE_URL = f"http://{CAM_IP}/capture"
CAM_STATUS_URL = f"http://{CAM_IP}/status"

# ============================================
# Flask App
# ============================================
app = Flask(__name__)
CORS(app)

# Local AI URL is used directly via requests

# Chat history for context
chat_history = []

# System prompts for the AI
SYSTEM_PROMPT_EN = """You are GEO, an advanced AI vision system mounted on an aerial drone camera.

Your role:
- Analyze images from the drone's live camera feed
- Describe scenes in detail (buildings, roads, people, vehicles, terrain, etc.)
- Answer the user's questions about what the camera sees
- If asked "what do you see?" - give a thorough scene description
- If asked about something specific - focus on that in the image
- Be precise about distances, directions, and spatial relationships

Your style:
- Respond in clear, concise English
- Be direct and informative like a tactical AI assistant
- If you can't see something the user asks about, say so honestly
- Use short paragraphs for readability"""

SYSTEM_PROMPT_AR = """You are GEO, an advanced AI vision system mounted on an aerial drone camera.
Respond in Arabic (Egyptian dialect). Analyze drone camera images and answer questions about what you see.
Be concise, friendly, and use emoji. Be honest if you can't see something."""


def set_cam_ip(ip):
    """Set ESP32-CAM IP address"""
    global CAM_IP, CAM_STREAM_URL, CAM_CAPTURE_URL, CAM_STATUS_URL
    CAM_IP = ip
    CAM_STREAM_URL = f"http://{ip}:81/stream"
    CAM_CAPTURE_URL = f"http://{ip}/capture"
    CAM_STATUS_URL = f"http://{ip}/status"


def set_drone_ip(ip):
    """Set Drone Controller IP address"""
    global DRONE_IP, DRONE_DATA_URL, DRONE_CONTROL_URL
    DRONE_IP = ip
    DRONE_DATA_URL = f"http://{ip}/data"
    DRONE_CONTROL_URL = f"http://{ip}/control"


def check_device(url, timeout=2):
    """Check if a device is reachable"""
    try:
        r = requests.get(url, timeout=timeout)
        return r.status_code == 200
    except:
        return False


def capture_frame():
    """Capture a single frame from ESP32-CAM and return as base64"""
    try:
        response = requests.get(CAM_CAPTURE_URL, timeout=5)
        if response.status_code == 200:
            img = Image.open(io.BytesIO(response.content))
            buf = io.BytesIO()
            img.save(buf, format='JPEG', quality=85)
            img_bytes = buf.getvalue()
            
            if len(img_bytes) > 3 * 1024 * 1024:
                buf = io.BytesIO()
                img = img.resize((img.width // 2, img.height // 2))
                img.save(buf, format='JPEG', quality=70)
                img_bytes = buf.getvalue()
            
            return base64.b64encode(img_bytes).decode('utf-8')
    except Exception as e:
        print(f"[ERROR] Frame capture failed: {e}")
    return None


def analyze_with_ai(message, image_base64=None, lang='en'):
    """Send message (and optional image) to Local Ollama AI"""
    try:
        prompt = SYSTEM_PROMPT_AR if lang == 'ar' else SYSTEM_PROMPT_EN
        messages = [{"role": "system", "content": prompt}]
        messages.extend(chat_history[-6:])
        
        user_msg = {"role": "user", "content": message}
        
        if image_base64:
            user_msg["images"] = [image_base64]
            
        messages.append(user_msg)
        
        payload = {
            "model": OLLAMA_MODEL,
            "messages": messages,
            "stream": False,
            "options": {
                "temperature": 0.7
            }
        }
        
        response = requests.post(OLLAMA_URL, json=payload, timeout=30)
        response.raise_for_status()
        
        ai_response = response.json().get("message", {}).get("content", "")
        
        chat_history.append({"role": "user", "content": message})
        chat_history.append({"role": "assistant", "content": ai_response})
        
        if len(chat_history) > 20:
            chat_history[:] = chat_history[-12:]
        
        return ai_response
    
    except Exception as e:
        error_msg = str(e)
        print(f"[ERROR] Ollama API: {error_msg}")
        return f"Error: {error_msg}"


# ============================================
# Routes — Main UI
# ============================================

@app.route('/')
def index():
    """Serve the unified drone control + camera + AI interface"""
    return render_template('index.html')


@app.route('/settings')
def settings():
    """Serve the Trim + PID tuning settings page"""
    return render_template('settings.html')


# ============================================
# Routes — System Status
# ============================================

@app.route('/api/status')
def api_status():
    """Get full system status"""
    drone_online = check_device(DRONE_DATA_URL)
    cam_online = check_device(CAM_STATUS_URL)
    
    cam_status = None
    if cam_online:
        try:
            r = requests.get(CAM_STATUS_URL, timeout=2)
            cam_status = r.json()
        except:
            pass
    
    return jsonify({
        "server": "online",
        "drone_ip": DRONE_IP,
        "drone_connected": drone_online,
        "cam_ip": CAM_IP,
        "cam_connected": cam_online,
        "cam_status": cam_status,
        "stream_url": CAM_STREAM_URL,
        "ollama_model": OLLAMA_MODEL
    })


# ============================================
# Routes — Drone Control (Proxy to ESP32 Controller)
# ============================================

@app.route('/api/drone/data')
def drone_data():
    """Proxy drone sensor data (pitch, roll, yaw, throttle)"""
    try:
        r = requests.get(DRONE_DATA_URL, timeout=2)
        return Response(r.text, mimetype='application/json')
    except Exception as e:
        return jsonify({"error": str(e), "pitch": 0, "roll": 0, "yaw": 0, "throttle": 1000}), 503


@app.route('/api/drone/control')
def drone_control():
    """Forward control commands to drone"""
    cmd = request.args.get('cmd', '')
    if not cmd:
        return jsonify({"error": "No command"}), 400
    try:
        r = requests.get(f"{DRONE_CONTROL_URL}?cmd={cmd}", timeout=2)
        return Response(r.text, mimetype='text/plain')
    except Exception as e:
        return jsonify({"error": str(e)}), 503


@app.route('/api/drone/set_ip')
def drone_set_ip():
    """Manually set drone controller IP"""
    ip = request.args.get('ip', '')
    if not ip:
        return jsonify({"error": "IP required"}), 400
    set_drone_ip(ip)
    connected = check_device(DRONE_DATA_URL)
    return jsonify({"success": True, "ip": ip, "connected": connected})


# ============================================
# Routes — Camera (Proxy to ESP32-CAM)
# ============================================

@app.route('/api/cam/set_ip')
def cam_set_ip():
    """Manually set camera IP"""
    ip = request.args.get('ip', '')
    if not ip:
        return jsonify({"error": "IP required"}), 400
    set_cam_ip(ip)
    connected = check_device(CAM_STATUS_URL)
    return jsonify({
        "success": True, "ip": ip, "connected": connected,
        "stream_url": CAM_STREAM_URL
    })


@app.route('/video_feed')
def video_feed():
    """Proxy the MJPEG stream from ESP32-CAM"""
    def generate():
        try:
            r = requests.get(CAM_STREAM_URL, stream=True, timeout=10)
            for chunk in r.iter_content(chunk_size=4096):
                yield chunk
        except Exception as e:
            print(f"[ERROR] Stream proxy failed: {e}")
    
    return Response(
        generate(),
        mimetype='multipart/x-mixed-replace; boundary=123456789000000000000987654321'
    )


@app.route('/api/capture')
def api_capture():
    """Capture a frame and return it"""
    frame = capture_frame()
    if frame:
        return jsonify({"success": True, "image": frame})
    return jsonify({"success": False, "error": "Capture failed"}), 500


@app.route('/api/flash')
def api_flash():
    """Control ESP32-CAM flash LED"""
    state = request.args.get('state', '0')
    try:
        r = requests.get(f"http://{CAM_IP}/flash?state={state}", timeout=2)
        return jsonify(r.json())
    except:
        return jsonify({"error": "Failed to control flash"}), 500


@app.route('/api/resolution')
def api_resolution():
    """Change camera resolution"""
    val = request.args.get('val', '5')
    try:
        r = requests.get(f"http://{CAM_IP}/resolution?val={val}", timeout=2)
        return jsonify(r.json())
    except:
        return jsonify({"error": "Failed to change resolution"}), 500


@app.route('/api/cam/rssi')
def cam_rssi():
    """Get WiFi signal strength (RSSI) from ESP32-CAM"""
    try:
        r = requests.get(CAM_STATUS_URL, timeout=2)
        data = r.json()
        rssi = data.get('rssi', None)
        return jsonify({"rssi": rssi, "ip": CAM_IP})
    except Exception as e:
        return jsonify({"error": str(e), "rssi": None}), 503


# ============================================
# Routes — AI Chat
# ============================================

@app.route('/api/chat', methods=['POST'])
def api_chat():
    """Chat with GEO AI — analyzes current camera view"""
    data = request.get_json()
    if not data or 'message' not in data:
        return jsonify({"error": "Message required"}), 400
    
    message = data['message']
    include_image = data.get('include_image', True)
    lang = data.get('lang', 'en')
    
    image_base64 = None
    if include_image:
        image_base64 = capture_frame()
    
    response = analyze_with_ai(message, image_base64, lang)
    
    return jsonify({
        "response": response,
        "has_image": image_base64 is not None,
        "timestamp": time.strftime("%H:%M:%S")
    })


VOICE_SYSTEM_PROMPT = """You are GEO, an AI drone assistant that can both analyze camera images AND control the drone via voice commands.

When the user gives a voice command, you MUST respond with ONLY a valid JSON object (no markdown, no code blocks):
{"reply": "your spoken response", "action": "command_name_or_null"}

Available drone commands (use exactly these strings):
- "takeoff" - take off
- "land" - land the drone
- "stop" - emergency stop all motors
- "forward" - fly forward
- "backward" - fly backward
- "left" - fly left (roll left)
- "right" - fly right (roll right)
- "stop_dir" - stop directional movement
- "yaw_left" - rotate left
- "yaw_right" - rotate right
- "stop_yaw" - stop rotation
- "speed1", "speed2", "speed3", "speed4" - set speed level

If the user asks a question (not a command), set action to null and just answer.
If you also have an image, describe what you see when relevant.

Examples:
- "take off" → {"reply": "Taking off! Motors engaged.", "action": "takeoff"}
- "go forward" → {"reply": "Moving forward.", "action": "forward"}
- "stop" → {"reply": "Emergency stop! All motors off.", "action": "stop"}
- "what do you see?" → {"reply": "I can see a park with trees...", "action": null}
- "turn left" → {"reply": "Rotating left.", "action": "yaw_left"}
- "speed 3" → {"reply": "Speed level 3 set.", "action": "speed3"}
- "land now" → {"reply": "Landing the drone.", "action": "land"}
- "go right slowly" → {"reply": "Moving right at current speed.", "action": "right"}

CRITICAL: Output ONLY the JSON object. No extra text, no markdown formatting."""


@app.route('/api/voice_command', methods=['POST'])
def api_voice_command():
    """Process voice commands — GEO interprets and controls the drone"""
    data = request.get_json()
    if not data or 'text' not in data:
        return jsonify({"error": "Voice text required"}), 400
    
    voice_text = data['text']
    include_image = data.get('include_image', True)
    
    # Capture frame if camera is available
    image_base64 = None
    if include_image:
        image_base64 = capture_frame()
    
    try:
        messages = [{"role": "system", "content": VOICE_SYSTEM_PROMPT}]
        
        user_msg = {"role": "user", "content": voice_text}
        if image_base64:
            user_msg["images"] = [image_base64]
            
        messages.append(user_msg)
        
        payload = {
            "model": OLLAMA_MODEL,
            "messages": messages,
            "stream": False,
            "options": {
                "temperature": 0.3
            }
        }
        
        response = requests.post(OLLAMA_URL, json=payload, timeout=30)
        response.raise_for_status()
        
        ai_text = response.json().get("message", {}).get("content", "").strip()
        
        # Parse JSON response from AI
        import json
        try:
            # Clean up potential markdown formatting
            if ai_text.startswith('```'):
                ai_text = ai_text.split('\n', 1)[1].rsplit('```', 1)[0].strip()
            result = json.loads(ai_text)
        except json.JSONDecodeError:
            result = {"reply": ai_text, "action": None}
        
        reply = result.get("reply", ai_text)
        action = result.get("action", None)
        
        # Execute drone command if present
        executed = False
        if action and action != "null":
            try:
                requests.get(f"{DRONE_CONTROL_URL}?cmd={action}", timeout=2)
                executed = True
            except:
                executed = False
        
        return jsonify({
            "reply": reply,
            "action": action,
            "executed": executed,
            "timestamp": time.strftime("%H:%M:%S")
        })
        
    except Exception as e:
        return jsonify({"reply": f"Error: {str(e)}", "action": None, "executed": False}), 500

@app.route('/api/clear_history', methods=['POST'])
def api_clear_history():
    """Clear chat history"""
    chat_history.clear()
    return jsonify({"success": True})


# ============================================
# Main
# ============================================
if __name__ == '__main__':
    import sys
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    
    print("\n" + "="*55)
    print("  FALCON — Unified Drone Command Server v2.0")
    print("="*55)
    print(f"\n  Drone Controller IP : {DRONE_IP}")
    print(f"  Camera IP           : {CAM_IP}")
    print(f"  AI Model            : jericho > Geo")
    
    print("\n[INFO] Checking devices on SkyHund...")
    
    drone_ok = check_device(DRONE_DATA_URL)
    cam_ok = check_device(CAM_STATUS_URL)
    
    print(f"  Drone : {'✅ Online' if drone_ok else '❌ Offline'}")
    print(f"  Camera: {'✅ Online' if cam_ok else '❌ Offline'}")
    
    if not drone_ok or not cam_ok:
        print("\n[TIP] Make sure:")
        print("  1. Drone is powered on (AP: SkyHund)")
        print("  2. Camera is powered on (will connect to SkyHund)")
        print("  3. Your PC is connected to WiFi: SkyHund (pass: 12345678)")
        print("  4. You can set IPs manually in the web interface")
    
    print(f"\n[SERVER] Starting on http://localhost:5000")
    print("="*55 + "\n")
    
    app.run(host='0.0.0.0', port=5000, debug=True, threaded=True)
