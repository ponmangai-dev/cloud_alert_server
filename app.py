from flask import Flask, request
from twilio.rest import Client
import os

app = Flask(__name__)

# Load secrets from Render Environment Variables
TWILIO_SID = os.getenv("TWILIO_SID")
TWILIO_AUTH = os.getenv("TWILIO_AUTH")
TWILIO_NUMBER = os.getenv("TWILIO_NUMBER")   # Your Twilio Phone Number
TARGET_NUMBER = os.getenv("TARGET_NUMBER")   # The number where alert should be sent

client = Client(TWILIO_SID, TWILIO_AUTH)

@app.route("/alert", methods=["POST"])
def alert():
    data = request.json
    msg = data.get("message", "Alert received")

    # Send SMS
    message = client.messages.create(
        body=msg,
        from_=TWILIO_NUMBER,
        to=TARGET_NUMBER
    )

    return {"status": "ok", "sid": message.sid}

@app.route("/")
def home():
    return "Cloud Server Running"
