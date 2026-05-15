"""
Weather IoT Lambda handler.

Receives POSTs from the ESP32 (via Lambda Function URL) and writes each
reading as a row in DynamoDB.

Expected JSON body:
    {
      "device_id": "esp32poc001",
      "temp":    22.5,
      "humidity":  45,
      "ts":        35420         # ms since ESP32 boot (informational)
    }
"""

import json
import logging
import os
from datetime import datetime, timezone
from decimal import Decimal

import boto3

# -----------------------------------------------------------------------------
# Setup
# -----------------------------------------------------------------------------
logger = logging.getLogger()
logger.setLevel(logging.INFO)

TABLE_NAME = os.environ["TABLE_NAME"]

# Create the DynamoDB client OUTSIDE the handler so it's reused across warm
# Lambda invocations. This is a free perf win.
dynamodb = boto3.resource("dynamodb")
table = dynamodb.Table(TABLE_NAME)


# -----------------------------------------------------------------------------
# Helpers
# -----------------------------------------------------------------------------
def _response(status_code: int, body: dict) -> dict:
    """Build a standard Lambda Function URL response."""
    return {
        "statusCode": status_code,
        "headers": {"Content-Type": "application/json"},
        "body": json.dumps(body),
    }


def _to_decimal(value):
    """
    DynamoDB doesn't accept float — only Decimal. Convert via str to avoid
    floating-point representation quirks.
    """
    if value is None:
        return None
    return Decimal(str(value))


# -----------------------------------------------------------------------------
# Lambda entry point
# -----------------------------------------------------------------------------
def lambda_handler(event, context):
    try:
        # ---- Parse JSON body ----
        # Lambda Function URL puts the body as a string in event["body"].
        body_str = event.get("body", "{}")
        data = json.loads(body_str)

        device_id = data.get("device_id")
        temp    = data.get("temp")
        humidity  = data.get("humidity")
        device_ts = data.get("ts")    # ms since boot, informational

        # ---- Validate required fields ----
        if not device_id:
            return _response(400, {"error": "missing device_id"})
        if temp is None or humidity is None:
            return _response(400, {"error": "missing temp_f or humidity"})

        # ---- Server-side timestamp (the authoritative one) ----
        now = datetime.now(timezone.utc)
        server_ts_ms = int(now.timestamp() * 1000)
        server_iso   = now.isoformat()

        # ---- Write row to DynamoDB ----
        item = {
            "device_id":  device_id,
            "ts":         server_ts_ms,
            "temp":     _to_decimal(temp),
            "humidity":   _to_decimal(humidity),
            "device_ms":  _to_decimal(device_ts) if device_ts is not None else None,
            "server_iso": server_iso,
        }
        # Drop None values (DynamoDB rejects them)
        item = {k: v for k, v in item.items() if v is not None}

        table.put_item(Item=item)

        logger.info(
            "TELEMETRY device=%s temp=%s humidity=%s server_iso=%s",
            device_id, temp, humidity, server_iso,
        )

        return _response(200, {
            "status": "ok",
            "server_time": server_iso,
            "ts": server_ts_ms,
        })

    except json.JSONDecodeError as e:
        logger.warning("Bad JSON: %s", e)
        return _response(400, {"error": "invalid JSON"})

    except Exception as e:
        logger.exception("Unhandled error")
        return _response(500, {"error": str(e)})
