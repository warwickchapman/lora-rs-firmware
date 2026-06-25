#!/usr/bin/env python3
import sys
import json
import time
import argparse
import random

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("Error: 'paho-mqtt' library is required to run this script.")
    print("Please install it using: pip install paho-mqtt")
    sys.exit(1)

def main():
    parser = argparse.ArgumentParser(description="MQTT Challenge-Session validation script")
    parser.add_argument("--host", default="localhost", help="MQTT Broker host (default: localhost)")
    parser.add_argument("--port", type=int, default=1883, help="MQTT Broker port (default: 1883)")
    parser.add_argument("--root", default="lrs", help="MQTT topic root (default: lrs)")
    parser.add_argument("--chip-id", required=True, help="Target Gateway Chip ID (hex, e.g. 000af8e6)")
    parser.add_argument("--password", required=True, help="Gateway admin password")
    args = parser.parse_args()

    chip_id_clean = args.chip_id.lower().replace("lrs-", "")
    cmd_topic = f"{args.root}/lrs-{chip_id_clean}/admin_command"
    resp_topic = f"{args.root}/lrs-{chip_id_clean}/admin_response"

    responses = {}

    def on_connect(client, userdata, flags, rc):
        if rc == 0:
            print(f"Connected to MQTT broker at {args.host}:{args.port}")
            client.subscribe(resp_topic)
            print(f"Subscribed to response topic: {resp_topic}")
        else:
            print(f"Connection failed with code {rc}")
            sys.exit(1)

    def on_message(client, userdata, msg):
        try:
            payload = json.loads(msg.payload.decode())
            req_id = payload.get("id")
            if req_id:
                responses[req_id] = payload
        except Exception as e:
            print(f"Failed to parse incoming message: {e}")

    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(args.host, args.port, 60)
    except Exception as e:
        print(f"Failed to connect to broker: {e}")
        sys.exit(1)

    client.loop_start()
    time.sleep(1.0) # wait for connection and subscription

    def send_and_await(payload, timeout=5.0):
        req_id = payload["id"]
        print(f"\n---> PUBLISH to {cmd_topic}:")
        print(json.dumps(payload, indent=2))
        client.publish(cmd_topic, json.dumps(payload))
        
        start = time.time()
        while req_id not in responses:
            if time.time() - start > timeout:
                print("x--- TIMEOUT waiting for response")
                return None
            time.sleep(0.1)
        
        resp = responses.pop(req_id)
        print("<--- RECEIVED response:")
        print(json.dumps(resp, indent=2))
        return resp

    print("======================================================================")
    print("STEP 1: Run admin_challenge without password, ts, or ttl_ms")
    print("======================================================================")
    challenge_req = {
        "id": f"req-{random.randint(100000, 999999)}",
        "cmd": "admin_challenge"
    }
    challenge_resp = send_and_await(challenge_req)
    if not challenge_resp or not challenge_resp.get("ok"):
        print("FAIL: admin_challenge failed.")
        sys.exit(1)
    
    session_id = challenge_resp.get("session_id")
    print(f"SUCCESS: Established session ID = {session_id}")

    print("\n======================================================================")
    print("STEP 2: Send command with valid session_id, seq=1, and password")
    print("======================================================================")
    cmd_req = {
        "id": f"req-{random.randint(100000, 999999)}",
        "cmd": "get_config",
        "session_id": session_id,
        "seq": 1,
        "admin_password": args.password
    }
    cmd_resp = send_and_await(cmd_req)
    if not cmd_resp or not cmd_resp.get("ok"):
        print("FAIL: Valid command execution failed.")
        sys.exit(1)
    print("SUCCESS: Command execution succeeded.")

    print("\n======================================================================")
    print("STEP 3: Replay same seq=1 request")
    print("======================================================================")
    replay_resp = send_and_await(cmd_req)
    if replay_resp and replay_resp.get("ok") is False and replay_resp.get("error") == "admin_sequence_replay":
        print("SUCCESS: Replayed request was correctly rejected with admin_sequence_replay.")
    else:
        print(f"FAIL: Replay rejection failed. Response: {replay_resp}")
        sys.exit(1)

    print("\n======================================================================")
    print("STEP 4: Send command with seq=2")
    print("======================================================================")
    cmd_req_seq2 = {
        "id": f"req-{random.randint(100000, 999999)}",
        "cmd": "get_config",
        "session_id": session_id,
        "seq": 2,
        "admin_password": args.password
    }
    cmd_resp_seq2 = send_and_await(cmd_req_seq2)
    if cmd_resp_seq2 and cmd_resp_seq2.get("ok"):
        print("SUCCESS: Command with seq=2 succeeded.")
    else:
        print("FAIL: Command with seq=2 failed.")
        sys.exit(1)

    print("\n======================================================================")
    print("STEP 5: Send command with invalid session_id")
    print("======================================================================")
    invalid_sess_req = {
        "id": f"req-{random.randint(100000, 999999)}",
        "cmd": "get_config",
        "session_id": 99999999,
        "seq": 3,
        "admin_password": args.password
    }
    invalid_sess_resp = send_and_await(invalid_sess_req)
    if invalid_sess_resp and invalid_sess_resp.get("ok") is False and invalid_sess_resp.get("error") == "admin_session_invalid":
        print("SUCCESS: Invalid session request was correctly rejected with admin_session_invalid.")
    else:
        print(f"FAIL: Invalid session rejection failed. Response: {invalid_sess_resp}")
        sys.exit(1)

    print("\n======================================================================")
    print("All MQTT verification steps completed successfully!")
    print("======================================================================")
    
    client.loop_stop()
    client.disconnect()

if __name__ == "__main__":
    main()
