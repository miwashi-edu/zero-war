/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║                    ⚔  ZERO WAR  ⚔                           ║
 * ║              Your Zero — C Edition                          ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * SETUP (Linux / WSL / Raspberry Pi):
 *   sudo apt install libmosquitto-dev
 *   gcc zero_war.c -o zero_war -lmosquitto && ./zero_war
 *
 * SETUP (macOS):
 *   brew install mosquitto
 *   gcc zero_war.c -o zero_war -lmosquitto && ./zero_war
 *
 * WHAT THIS SCRIPT DOES:
 *   1. Connects to the class MQTT broker over TLS
 *   2. Publishes your zero's status every 10 seconds
 *   3. Listens for incoming attacks and prints them
 *   4. Lets you attack another zero by typing their ID
 *
 * YOUR MISSION:
 *   Get it compiling and running, then improve it:
 *   - [ ] Track HP properly and update it when attacked
 *   - [ ] Print a bar like [########--] for HP
 *   - [ ] Add a cooldown between attacks (use time())
 *   - [ ] Print "GAME OVER" and exit when HP hits 0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <mosquitto.h>

/* ── CONFIGURATION ─────────────────────────────────────────────────────────
   Fill in your details before compiling! */

#define BROKER    "3db5059fc43148f4b17376a4bd9f28f2.s1.eu.hivemq.cloud"
#define PORT      8883
#define USERNAME  "YOUR_USERNAME"
#define PASSWORD  "YOUR_PASSWORD"

#define MY_ID     "c01"        /* Pick a unique short ID */
#define MY_NAME   "YourName"   /* Your display name      */
#define MY_TEAM   "A"          /* "A" or "B"             */

/* ── GAME STATE ────────────────────────────────────────────────────────────*/
int hp          = 100;
int game_active = 0;

/* We need a pointer to the mosquitto client in callbacks,
   so we store it globally here */
struct mosquitto *mosq = NULL;


/* ── HELPERS ───────────────────────────────────────────────────────────────*/

/* Build a simple JSON string without an external library.
   In real projects you'd use cJSON or json-c.
   buf must be large enough to hold the result. */
void build_status_json(char *buf, int bufsize) {
    snprintf(buf, bufsize,
        "{\"id\":\"%s\",\"name\":\"%s\",\"team\":\"%s\","
        "\"hp\":%d,\"alive\":%s}",
        MY_ID, MY_NAME, MY_TEAM,
        hp, (hp > 0) ? "true" : "false"
    );
}

/* Parse a single integer field from a flat JSON string.
   e.g. get_json_int("{\"damage\":15}", "damage")  →  15
   This is a simple approach — for complex JSON use cJSON. */
int get_json_int(const char *json, const char *key) {
    /* Look for "key": in the string */
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);

    const char *pos = strstr(json, search);
    if (!pos) return -1;

    /* Move past the key and colon to the value */
    pos += strlen(search);
    return atoi(pos);
}

/* Same idea, but extracts a string field into out_buf */
void get_json_str(const char *json, const char *key, char *out_buf, int out_size) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":\"", key);

    const char *pos = strstr(json, search);
    if (!pos) { out_buf[0] = '\0'; return; }

    pos += strlen(search);
    int i = 0;
    while (*pos && *pos != '"' && i < out_size - 1)
        out_buf[i++] = *pos++;
    out_buf[i] = '\0';
}

/* Publish our current HP and info to the scoreboard topic */
void publish_status(void) {
    char topic[64];
    char payload[256];

    snprintf(topic, sizeof(topic), "zerowars/status/%s", MY_ID);
    build_status_json(payload, sizeof(payload));

    /* retain=true means the broker keeps this message and sends it
       to any new subscriber immediately — great for status */
    mosquitto_publish(mosq, NULL, topic, strlen(payload), payload, 0, true);
    printf("📤 Status published: HP=%d\n", hp);
}

/* Attack a target zero by their ID */
void send_attack(const char *target_id) {
    char topic[64];
    char payload[128];

    /* Random damage 10–25 */
    int damage = 10 + (rand() % 16);

    snprintf(topic,   sizeof(topic),   "zerowars/attack/%s", target_id);
    snprintf(payload, sizeof(payload), "{\"from\":\"%s\",\"damage\":%d}", MY_ID, damage);

    mosquitto_publish(mosq, NULL, topic, strlen(payload), payload, 0, false);
    printf("⚡ Attacked %s for %d damage!\n", target_id, damage);
}


/* ── CALLBACKS ─────────────────────────────────────────────────────────────
   These are called automatically by the mosquitto library.
   You register them with mosquitto_*_callback_set() in main(). */

/* Called when connection to broker is established (or fails) */
void on_connect(struct mosquitto *m, void *userdata, int rc) {
    if (rc == 0) {
        printf("✅ Connected to HiveMQ as '%s' [%s]\n", MY_NAME, MY_ID);

        /* Subscribe to our attack topic and the teacher broadcast */
        char attack_topic[64];
        snprintf(attack_topic, sizeof(attack_topic), "zerowars/attack/%s", MY_ID);

        mosquitto_subscribe(m, NULL, attack_topic,      0);
        mosquitto_subscribe(m, NULL, "zerowars/broadcast", 0);

        printf("👂 Listening on: %s\n", attack_topic);

        /* Announce ourselves to the dashboard */
        publish_status();
    } else {
        /* rc values: 1=wrong protocol, 2=bad client ID, 3=broker unavailable,
                      4=bad credentials, 5=not authorised */
        fprintf(stderr, "❌ Connection failed. Return code: %d\n", rc);
        fprintf(stderr, "   Check USERNAME, PASSWORD, and BROKER.\n");
    }
}

/* Called every time a message arrives on a subscribed topic */
void on_message(struct mosquitto *m, void *userdata,
                const struct mosquitto_message *msg)
{
    /* msg->payload is NOT null-terminated, so we copy it into a proper string */
    char payload[512];
    int  len = msg->payloadlen < (int)sizeof(payload) - 1
                   ? msg->payloadlen
                   : (int)sizeof(payload) - 1;
    memcpy(payload, msg->payload, len);
    payload[len] = '\0';

    char my_attack_topic[64];
    snprintf(my_attack_topic, sizeof(my_attack_topic), "zerowars/attack/%s", MY_ID);

    /* ── Incoming attack ───────────────────────────────────── */
    if (strcmp(msg->topic, my_attack_topic) == 0) {
        int  damage   = get_json_int(payload, "damage");
        char attacker[32];
        get_json_str(payload, "from", attacker, sizeof(attacker));

        if (damage < 0) damage = 10;   /* default if parsing fails */

        hp -= damage;
        if (hp < 0) hp = 0;

        printf("\n💥 HIT by %s! -%d HP  |  HP remaining: %d\n",
               attacker, damage, hp);

        if (hp == 0) {
            printf("💀 YOU HAVE BEEN ELIMINATED. Better luck next round!\n");
        }

        /* TODO (your task): publish updated status after being hit */
        /* publish_status(); */
    }

    /* ── Teacher broadcast ─────────────────────────────────── */
    else if (strcmp(msg->topic, "zerowars/broadcast") == 0) {
        char cmd[32];
        get_json_str(payload, "cmd", cmd, sizeof(cmd));

        if (strcmp(cmd, "START") == 0) {
            hp          = 100;
            game_active = 1;
            printf("\n🚀 === GAME STARTED! === Fight!\n");
            publish_status();

        } else if (strcmp(cmd, "STOP") == 0) {
            game_active = 0;
            printf("\n🛑 === GAME OVER ===\n");

        } else if (strcmp(cmd, "PING") == 0) {
            printf("📡 PING received — responding...\n");
            publish_status();
        }
    }
}

/* Called when disconnected from broker */
void on_disconnect(struct mosquitto *m, void *userdata, int rc) {
    printf("\n⚠️  Disconnected (rc=%d). Reconnecting...\n", rc);
}


/* ── MAIN ──────────────────────────────────────────────────────────────────*/
int main(void) {
    srand((unsigned)time(NULL));

    /* mosquitto_lib_init() must be called once before anything else */
    mosquitto_lib_init();

    /* Create a new mosquitto client instance.
       Arguments: client ID string, clean_session=true, userdata pointer */
    char client_id[32];
    snprintf(client_id, sizeof(client_id), "zero-%s", MY_ID);
    mosq = mosquitto_new(client_id, true, NULL);

    if (!mosq) {
        fprintf(stderr, "❌ Failed to create mosquitto client\n");
        return 1;
    }

    /* Register callbacks */
    mosquitto_connect_callback_set(mosq,    on_connect);
    mosquitto_message_callback_set(mosq,    on_message);
    mosquitto_disconnect_callback_set(mosq, on_disconnect);

    /* Set credentials */
    mosquitto_username_pw_set(mosq, USERNAME, PASSWORD);

    /* Enable TLS — NULL arguments use system CA certificates */
    int rc = mosquitto_tls_set(mosq, NULL, NULL, NULL, NULL, NULL);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "❌ TLS setup failed: %s\n", mosquitto_strerror(rc));
        return 1;
    }

    /* Connect to the broker.
       keepalive=60 means send a PING every 60 seconds to stay connected */
    printf("🔌 Connecting to %s:%d ...\n", BROKER, PORT);
    rc = mosquitto_connect(mosq, BROKER, PORT, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "❌ Connect error: %s\n", mosquitto_strerror(rc));
        return 1;
    }

    /* Start the network loop in a background thread.
       This handles all send/receive operations automatically. */
    mosquitto_loop_start(mosq);

    /* Give it a moment to connect and trigger on_connect */
    sleep(2);

    /* ── Input loop ─────────────────────────────────────────── */
    printf("\nCommands:\n");
    printf("  attack <target_id>   attack another zero\n");
    printf("  status               print your current HP\n");
    printf("  quit                 exit\n\n");

    char line[128];
    while (1) {
        printf("> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;

        /* Strip newline */
        line[strcspn(line, "\n")] = '\0';

        if (strncmp(line, "attack ", 7) == 0) {
            const char *target = line + 7;   /* pointer arithmetic: skip "attack " */
            send_attack(target);

        } else if (strcmp(line, "status") == 0) {
            printf("HP: %d/100  |  Team: %s  |  ID: %s\n", hp, MY_TEAM, MY_ID);

        } else if (strcmp(line, "quit") == 0 || strcmp(line, "q") == 0) {
            printf("👋 Disconnecting...\n");
            break;

        } else if (line[0] != '\0') {
            printf("Unknown command. Try: attack <id> | status | quit\n");
        }
    }

    /* Clean shutdown */
    mosquitto_loop_stop(mosq, true);
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    return 0;
}
