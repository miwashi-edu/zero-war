"""
╔══════════════════════════════════════════════════════════════╗
║                    ⚔  ZERO WAR  ⚔                           ║
║              Your Zero — Python Edition                      ║
╚══════════════════════════════════════════════════════════════╝

SETUP:
    pip install paho-mqtt

WHAT THIS SCRIPT DOES:
    1. Connects to the class MQTT broker (HiveMQ)
    2. Publishes your zero's status every 10 seconds
    3. Listens for incoming attacks and prints them
    4. Lets you attack another zero by typing their ID

MQTT TOPICS USED:
    zerowars/status/<your_id>        ← you publish here
    zerowars/attack/<your_id>        ← enemies publish here (you listen)
    zerowars/attack/<target_id>      ← you publish here to attack
    zerowars/broadcast               ← teacher sends game commands

YOUR MISSION:
    Get the script running, then improve it:
    - [ ] Track your own HP and print it nicely
    - [ ] Add a cooldown so you can't spam attacks
    - [ ] Make it print a different message depending on how much damage you take
    - [ ] Print a "GAME OVER" message when your HP hits 0
"""

import paho.mqtt.client as mqtt
import json
import time
import threading
import random

# ── CONFIGURATION ─────────────────────────────────────────────────────────────
# Fill in YOUR details here before running!

BROKER   = "3db5059fc43148f4b17376a4bd9f28f2.s1.eu.hivemq.cloud"
PORT     = 8883          # TLS port
USERNAME = "YOUR_USERNAME"
PASSWORD = "YOUR_PASSWORD"

MY_ID    = "p01"         # Pick a unique short ID, e.g. your initials + number
MY_NAME  = "YourName"    # Your display name on the scoreboard
MY_TEAM  = "A"           # "A" or "B" — your teacher will tell you which

# ── GAME STATE ────────────────────────────────────────────────────────────────
hp = 100
game_active = False


# ── CALLBACKS ─────────────────────────────────────────────────────────────────
# These functions are called automatically when MQTT events happen.
# You don't call them yourself — the library calls them for you.

def on_connect(client, userdata, flags, rc, properties=None):
    """Called when the connection to the broker is established."""
    if rc == 0:
        print(f"✅ Connected to HiveMQ as '{MY_NAME}' [{MY_ID}]")

        # Subscribe to topics we want to RECEIVE
        client.subscribe(f"zerowars/attack/{MY_ID}")   # incoming attacks
        client.subscribe("zerowars/broadcast")          # teacher commands
        print(f"👂 Listening on: zerowars/attack/{MY_ID}")

        # Announce ourselves to the game dashboard
        publish_status(client)

    else:
        # rc = return code. Anything other than 0 means something went wrong.
        print(f"❌ Connection failed. Return code: {rc}")
        print("   Check your username, password, and broker address.")


def on_message(client, userdata, msg):
    """Called every time a message arrives on a subscribed topic."""
    global hp, game_active

    topic   = msg.topic
    payload = msg.payload.decode("utf-8")

    # Try to parse the message as JSON
    try:
        data = json.loads(payload)
    except json.JSONDecodeError:
        print(f"[?] Got non-JSON message on {topic}: {payload}")
        return

    # ── Handle incoming attack ────────────────────────────────
    if topic == f"zerowars/attack/{MY_ID}":
        damage    = data.get("damage", 10)
        attacker  = data.get("from", "unknown")

        hp -= damage
        hp  = max(0, hp)   # don't go below 0

        print(f"\n💥 HIT by {attacker}! -{damage} HP  |  HP remaining: {hp}")

        if hp == 0:
            print("💀 YOU HAVE BEEN ELIMINATED. Better luck next round!")

        # TODO (your task): publish your updated status after being hit
        # publish_status(client)

    # ── Handle teacher broadcast ──────────────────────────────
    elif topic == "zerowars/broadcast":
        command = data.get("cmd", "")

        if command == "START":
            hp          = 100
            game_active = True
            print("\n🚀 === GAME STARTED! === Fight!")
            publish_status(client)

        elif command == "STOP":
            game_active = False
            print("\n🛑 === GAME OVER ===")

        elif command == "PING":
            # Teacher is checking who's online
            print("📡 PING received — responding...")
            publish_status(client)


def on_disconnect(client, userdata, rc, properties=None):
    """Called when the connection is lost."""
    print(f"\n⚠️  Disconnected from broker (rc={rc}). Reconnecting...")


# ── PUBLISH HELPERS ───────────────────────────────────────────────────────────

def publish_status(client):
    """Send our current HP and info to the scoreboard."""
    message = {
        "id":     MY_ID,
        "name":   MY_NAME,
        "team":   MY_TEAM,
        "hp":     hp,
        "alive":  hp > 0,
    }
    # json.dumps() converts a Python dict → a JSON string
    payload = json.dumps(message)
    client.publish(f"zerowars/status/{MY_ID}", payload, retain=True)
    print(f"📤 Status published: HP={hp}")


def send_attack(client, target_id):
    """Attack another zero by their ID."""
    damage = random.randint(10, 25)    # random damage between 10 and 25

    message = {
        "from":   MY_ID,
        "damage": damage,
    }
    payload = json.dumps(message)
    client.publish(f"zerowars/attack/{target_id}", payload)
    print(f"⚡ Attacked {target_id} for {damage} damage!")


# ── HEARTBEAT THREAD ──────────────────────────────────────────────────────────
# This runs in the background and sends our status every 10 seconds.
# Threading lets two things happen at the same time: the heartbeat
# AND the input loop below.

def heartbeat_loop(client):
    """Publish status every 10 seconds so the dashboard knows we're alive."""
    while True:
        time.sleep(10)
        publish_status(client)


# ── MAIN ──────────────────────────────────────────────────────────────────────

def main():
    # Create an MQTT client object
    # CallbackAPIVersion.VERSION2 is required for paho-mqtt >= 2.0
    client = mqtt.Client(
        mqtt.CallbackAPIVersion.VERSION2,
        client_id=f"zero-{MY_ID}"
    )

    # Attach our callback functions
    client.on_connect    = on_connect
    client.on_message    = on_message
    client.on_disconnect = on_disconnect

    # Set credentials and enable TLS encryption
    client.username_pw_set(USERNAME, PASSWORD)
    client.tls_set()   # uses system's trusted CA certificates

    # Connect (this does not block — it just starts the process)
    print(f"🔌 Connecting to {BROKER}:{PORT} ...")
    client.connect(BROKER, PORT, keepalive=60)

    # Start the MQTT network loop in a background thread
    # This handles sending/receiving in the background automatically
    client.loop_start()

    # Give it a moment to connect before we start
    time.sleep(2)

    # Start heartbeat in a separate thread
    t = threading.Thread(target=heartbeat_loop, args=(client,), daemon=True)
    t.start()

    # ── Input loop — lets you type commands ───────────────────
    print("\nCommands:")
    print("  attack <target_id>   — attack another zero")
    print("  status               — print your current HP")
    print("  quit                 — exit\n")

    while True:
        try:
            command = input("> ").strip().lower()

            if command.startswith("attack "):
                target = command.split(" ", 1)[1]
                send_attack(client, target)

            elif command == "status":
                print(f"HP: {hp}/100  |  Team: {MY_TEAM}  |  ID: {MY_ID}")

            elif command in ("quit", "exit", "q"):
                print("👋 Disconnecting...")
                break

            else:
                print("Unknown command. Try: attack <id> | status | quit")

        except KeyboardInterrupt:
            break

    client.loop_stop()
    client.disconnect()


if __name__ == "__main__":
    main()
