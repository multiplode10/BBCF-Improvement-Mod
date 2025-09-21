#!/usr/bin/env python3
"""
Simple Mock API Server for BlazBlue CF Matchmaking Integration Testing
Run with: python test_api_server.py
"""

from http.server import HTTPServer, BaseHTTPRequestHandler
import json
import time
import urllib.parse
from datetime import datetime, timedelta
import threading
import sys

# In-memory storage for active rooms
active_rooms = {}
room_lock = threading.Lock()

class MatchmakingAPIHandler(BaseHTTPRequestHandler):
    def do_OPTIONS(self):
        """Handle CORS preflight requests"""
        self.send_response(200)
        self._set_cors_headers()
        self.end_headers()

    def do_GET(self):
        """Handle GET requests - list active rooms"""
        parsed_path = urllib.parse.urlparse(self.path)

        if parsed_path.path == '/api/rooms' or parsed_path.path == '/api/matches/active':
            self._handle_list_rooms()
        if parsed_path.path.startswith('/api/rooms/'):
            room_id = parsed_path.path.split('/')[-1]
            self._handle_get_room(room_id)
        elif parsed_path.path.startswith('/api/player/') and parsed_path.path.endswith('/history'):
            # Extract steam_id from path like /api/player/76561198123456789/history
            path_parts = parsed_path.path.split('/')
            if len(path_parts) >= 4:
                steam_id = path_parts[3]
                self._handle_get_player_rating(steam_id)
            else:
                self._send_error(400, "Invalid player endpoint format")
        else:
            self._send_error(404, "Endpoint not found")

    def do_POST(self):
        """Handle POST requests - create or update rooms"""
        parsed_path = urllib.parse.urlparse(self.path)

        if parsed_path.path == '/api/rooms':
            self._handle_create_update_room()
        else:
            self._send_error(404, "Endpoint not found")

    def do_DELETE(self):
        """Handle DELETE requests - remove rooms"""
        parsed_path = urllib.parse.urlparse(self.path)

        if parsed_path.path.startswith('/api/rooms/'):
            room_id = parsed_path.path.split('/')[-1]
            self._handle_delete_room(room_id)
        else:
            self._send_error(404, "Endpoint not found")

    def _handle_list_rooms(self):
        """Return list of all active rooms"""
        with room_lock:
            rooms_list = list(active_rooms.values())

        self._send_json_response(200, {
            "status": "success",
            "count": len(rooms_list),
            "rooms": rooms_list
        })

        print(f"[{self._timestamp()}] LIST ROOMS - Returned {len(rooms_list)} active rooms")

    def _handle_get_room(self, room_id):
        """Return specific room by ID"""
        with room_lock:
            room = active_rooms.get(room_id)

        if room:
            self._send_json_response(200, {
                "status": "success",
                "room": room
            })
            print(f"[{self._timestamp()}] GET ROOM - Found room: {room_id}")
        else:
            self._send_error(404, f"Room not found: {room_id}")

    def _handle_create_update_room(self):
        """Create or update a room"""
        try:
            # Read request body
            content_length = int(self.headers.get('Content-Length', 0))
            if content_length == 0:
                self._send_error(400, "Empty request body")
                return

            body = self.rfile.read(content_length).decode('utf-8')
            room_data = json.loads(body)

            # Validate required fields
            required_fields = ['id', 'host_steamid64', 'region', 'mode', 'capacity', 'open_slots']
            for field in required_fields:
                if field not in room_data:
                    self._send_error(400, f"Missing required field: {field}")
                    return

            # Add timestamps
            current_time = int(time.time())
            room_data['created_at'] = room_data.get('created_at', current_time)
            room_data['updated_at'] = current_time
            room_data['expires_at'] = current_time + 300  # 5 minutes expiry

            # Store room
            room_id = room_data['id']
            with room_lock:
                was_update = room_id in active_rooms
                active_rooms[room_id] = room_data

            # Respond with success
            self._send_json_response(200, {
                "status": "room created or updated",
                "id": room_id,
                "action": "updated" if was_update else "created"
            })

            action = "UPDATED" if was_update else "CREATED"
            print(f"[{self._timestamp()}] {action} ROOM - ID: {room_id}")
            print(f"  Host: {room_data['host_steamid64']}")
            print(f"  Region: {room_data['region']} ({'Region-Only' if room_data.get('is_region_only', False) else 'Global'})")
            print(f"  Mode: {room_data['mode']}")
            print(f"  Rating: {room_data.get('target_rating', 'N/A')}")
            print(f"  Steam URL: {room_data.get('steam_url', 'N/A')}")

        except json.JSONDecodeError:
            self._send_error(400, "Invalid JSON in request body")
        except Exception as e:
            self._send_error(500, f"Server error: {str(e)}")

    def _handle_delete_room(self, room_id):
        """Delete a room"""
        with room_lock:
            if room_id in active_rooms:
                room_data = active_rooms.pop(room_id)
                self._send_json_response(200, {
                    "status": "room deleted",
                    "id": room_id
                })

                print(f"[{self._timestamp()}] DELETED ROOM - ID: {room_id}")
                print(f"  Host: {room_data.get('host_steamid64', 'Unknown')}")
                print(f"  Region: {room_data.get('region', 'Unknown')}")

            else:
                self._send_error(404, f"Room not found: {room_id}")

    def _handle_get_player_rating(self, steam_id):
        """Return player rating and history"""
        try:
            # Validate steam_id (should be numeric for SteamID64)
            if steam_id.isdigit():
                steam_id_int = int(steam_id)

                # Mock player data based on SteamID
                # Use modulo to create different ratings for different players
                base_rating = 1200 + (steam_id_int % 1000)
                matches_played = 20 + (steam_id_int % 50)
                wins = int(matches_played * 0.6)  # 60% win rate
                losses = matches_played - wins

                player_data = {
                    "player_id": steam_id,
                    "steamid64": steam_id_int,
                    "rating": base_rating,
                    "matches_played": matches_played,
                    "wins": wins,
                    "losses": losses,
                    "region": "NA",
                    "last_played": "2024-01-15T10:30:00Z",
                    "rank": "Intermediate" if base_rating < 1600 else "Advanced"
                }

                self._send_json_response(200, player_data)

                print(f"[{self._timestamp()}] GET PLAYER RATING - SteamID: {steam_id}")
                print(f"  Rating: {base_rating}")
                print(f"  Matches: {matches_played} ({wins}W/{losses}L)")

            else:
                # Handle UUID format or other identifiers
                player_data = {
                    "player_id": steam_id,
                    "rating": 1500,
                    "matches_played": 25,
                    "wins": 15,
                    "losses": 10,
                    "region": "NA",
                    "last_played": "2024-01-15T10:30:00Z",
                    "rank": "Intermediate"
                }

                self._send_json_response(200, player_data)

                print(f"[{self._timestamp()}] GET PLAYER RATING - Player ID: {steam_id}")
                print(f"  Rating: 1500 (default for non-numeric ID)")

        except Exception as e:
            self._send_error(500, f"Error retrieving player data: {str(e)}")

    def _send_json_response(self, status_code, data):
        """Send JSON response"""
        self.send_response(status_code)
        self._set_cors_headers()
        self.send_header('Content-Type', 'application/json')
        self.end_headers()

        response = json.dumps(data, indent=2)
        self.wfile.write(response.encode('utf-8'))

    def _send_error(self, status_code, message):
        """Send error response"""
        self.send_response(status_code)
        self._set_cors_headers()
        self.send_header('Content-Type', 'application/json')
        self.end_headers()

        error_response = {
            "status": "error",
            "error": message,
            "timestamp": int(time.time())
        }

        response = json.dumps(error_response, indent=2)
        self.wfile.write(response.encode('utf-8'))

        print(f"[{self._timestamp()}] ERROR {status_code} - {message}")

    def _set_cors_headers(self):
        """Set CORS headers for web interface compatibility"""
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, DELETE, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type, Authorization')

    def _timestamp(self):
        """Get formatted timestamp for logging"""
        return datetime.now().strftime('%Y-%m-%d %H:%M:%S')

    def log_message(self, format, *args):
        """Override default logging to customize format"""
        timestamp = self._timestamp()
        method = getattr(self, 'command', 'UNKNOWN')
        path = getattr(self, 'path', 'unknown')
        print(f"[{timestamp}] {method} {path} - {format % args}")


def cleanup_expired_rooms():
    """Background task to remove expired rooms"""
    while True:
        current_time = int(time.time())
        expired_rooms = []

        with room_lock:
            for room_id, room_data in active_rooms.items():
                if room_data.get('expires_at', 0) < current_time:
                    expired_rooms.append(room_id)

            for room_id in expired_rooms:
                room_data = active_rooms.pop(room_id)
                print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] EXPIRED ROOM - ID: {room_id}")
                print(f"  Host: {room_data.get('host_steamid64', 'Unknown')}")

        time.sleep(30)  # Check every 30 seconds


def print_startup_info():
    """Print startup information and usage instructions"""
    print("=" * 60)
    print("BlazBlue CF Matchmaking API Mock Server")
    print("=" * 60)
    print("Server running on: http://localhost:8080")
    print("")
    print("Available endpoints:")
    print("  GET  /api/rooms         - List all active rooms")
    print("  POST /api/rooms         - Create or update room")
    print("  DELETE /api/rooms/{id}  - Delete specific room")
    print("  GET  /api/matches/active - Alternative endpoint for room list")
    print("  GET  /api/player/{steamid}/history - Get player rating and stats")
    print("")
    print("Testing with curl:")
    print("  curl http://localhost:8080/api/rooms")
    print("  curl http://localhost:8080/api/player/76561198123456789/history")
    print("")
    print("Integration testing:")
    print("  1. Start this server")
    print("  2. Launch BBCF with the Improvement Mod")
    print("  3. Create a ranked room with name 'RANKED'")
    print("  4. Watch the console for API calls")
    print("")
    print("Press Ctrl+C to stop the server")
    print("=" * 60)


def main():
    """Main server function"""
    if len(sys.argv) > 1:
        port = int(sys.argv[1])
    else:
        port = 8080

    print_startup_info()

    # Start cleanup thread
    cleanup_thread = threading.Thread(target=cleanup_expired_rooms, daemon=True)
    cleanup_thread.start()

    # Start HTTP server
    server_address = ('', port)
    httpd = HTTPServer(server_address, MatchmakingAPIHandler)

    try:
        print(f"\nStarting server on port {port}...\n")
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n\nShutting down server...")
        httpd.shutdown()
        print("Server stopped.")


if __name__ == '__main__':
    main()
