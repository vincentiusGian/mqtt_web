import streamlit as st
import ssl
import time
import threading
import queue
from paho.mqtt import client as mqtt

# MQTT config
broker = "broker.emqx.io"
port = 8883
topic_countdown = "pompa/countdown"
topic_waiting = "pompa/waiting"
topic_warning = "pompa/warning"
topic_status = "pompa/status"

# Queues
q_countdown = queue.Queue()
q_waiting = queue.Queue()
q_warning = queue.Queue()
q_status = queue.Queue()

# MQTT callbacks
def on_connect(client, userdata, flags, rc):
    print("Connected with result code " + str(rc))
    client.subscribe(topic_countdown)
    client.subscribe(topic_waiting)
    client.subscribe(topic_warning)
    client.subscribe(topic_status)

def on_message(client, userdata, msg):
    payload = msg.payload.decode()
    print(f"[MQTT] {msg.topic}: {payload}")
    if msg.topic == topic_countdown:
        q_countdown.put(payload)
    elif msg.topic == topic_waiting:
        q_waiting.put(payload)
    elif msg.topic == topic_warning:
        q_warning.put(payload)
    elif msg.topic == topic_status:
        q_status.put(payload)

def start_mqtt():
    client = mqtt.Client()
    client.tls_set(cert_reqs=ssl.CERT_NONE)
    client.tls_insecure_set(True)
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(broker, port)
    client.loop_forever()

# Start MQTT once
if "mqtt_started" not in st.session_state:
    threading.Thread(target=start_mqtt, daemon=True).start()
    st.session_state.mqtt_started = True

# UI setup
st.set_page_config(page_title="Smart Watering", layout="centered")
st.title("Smart Hydroponic Watering System")
st.caption("Tracking the system...")

message_placeholder = st.empty()
waiting_placeholder = st.empty()
warning_placeholder = st.empty()

# Formatting functions
def convert_to_metric(countdown):
    try:
        n = int(countdown)
        if n <= 0:
            return "✅ Finished, waiting for 30s..."
        elif n <= 5:
            return f"⚠️ Watering... {n}s left"
        else:
            return f"⏳ Watering... {n}s left"
    except:
        return "..."

def convert_waiting(countdown):
    try:
        n = int(countdown)
        if n > 0:
            return f"⏳ Please wait... Watering will resume in {n} seconds."
        else:
            return ""
    except:
        return ""

# State init
if "warning" not in st.session_state:
    st.session_state.warning = ""

if "refill_notified" not in st.session_state:
    st.session_state.refill_notified = False

if "finished_time" not in st.session_state:
    st.session_state.finished_time = None

# Main loop
while True:
    # --- Watering Countdown ---
    while not q_countdown.empty():
        msg = q_countdown.get()
        st.session_state["last_msg"] = msg
        text = convert_to_metric(msg)

        message_placeholder.markdown(
            f"<h4 style='text-align:center'>{text}</h4>",
            unsafe_allow_html=True
        )
        waiting_placeholder.empty()

        # Save finished time to hide later
        if text.startswith("✅ Finished"):
            st.session_state.finished_time = time.time()
        else:
            st.session_state.finished_time = None

    # --- Waiting Countdown ---
    while not q_waiting.empty():
        msg = q_waiting.get()
        text = convert_waiting(msg)
        if text:
            waiting_placeholder.markdown(
                f"""
                <div style='display: flex; justify-content: center;'>
                    <h4 style='color: orange; text-align: center;'>{text}</h4>
                </div>
                """,
                unsafe_allow_html=True
            )
            message_placeholder.empty()

    # --- Warnings ---
    while not q_warning.empty():
        warning_msg = q_warning.get().strip()
        if warning_msg:
            st.session_state.warning = warning_msg
            st.session_state.refill_notified = False
            warning_placeholder.error(f"🚨 {warning_msg}")
        else:
            # If empty warning received, clear box
            st.session_state.warning = ""
            warning_placeholder.empty()

    # --- Status (Check refill state) ---
    while not q_status.empty():
        status_msg = q_status.get().strip().upper()
        if status_msg == "NYALA" and st.session_state.warning and not st.session_state.refill_notified:
            warning_placeholder.success("✅ Water has been refilled. System resumed.")
            st.session_state.warning = ""
            st.session_state.refill_notified = True

    # Clear "✅ Finished..." after 5 seconds
    if st.session_state.finished_time:
        if time.time() - st.session_state.finished_time > 30:
            message_placeholder.empty()
            st.session_state.finished_time = None

    time.sleep(1)
