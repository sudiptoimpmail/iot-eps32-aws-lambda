# Weather IoT — ESP32 → AWS Lambda → DynamoDB

A minimal end-to-end IoT project. ESP32 reads DHT11 sensor, sends temperature
and humidity to an AWS Lambda Function URL over HTTPS, Lambda writes the data
into a DynamoDB table.

## Architecture

```
ESP32 (DHT11 + OLED)
   │
   │ HTTPS POST every 30s
   │ { device_id, temp_f, humidity }
   ▼
Lambda Function URL  (no API Gateway, no auth)
   │
   ▼
Lambda function (Python 3.12)
   │
   ▼
DynamoDB table  (one row per reading)
```

## Region
`us-east-2` (Ohio)

## Project structure

```
weather_iot/
├── README.md                 ← you are here
├── terraform/
│   ├── main.tf               ← all AWS resources
│   ├── variables.tf          ← knobs you can change
│   └── outputs.tf            ← prints the Lambda URL after apply
├── lambda/
│   └── handler.py            ← the Python Lambda function code
└── esp32/
    └── weather_station.ino   ← Arduino sketch for ESP32
```

## Deployment in 5 commands

Assumes you have Terraform installed and AWS credentials configured
(`aws configure` or env vars).

```bash
cd terraform
terraform init
terraform plan
terraform apply
# Note the lambda_function_url output — copy it
```

Then edit `esp32/weather_station.ino`, paste the URL into `LAMBDA_URL`, set
your WiFi credentials, and upload to the ESP32 from Arduino IDE.

## What this costs

**$0/month indefinitely** with AWS Free Tier:
- Lambda: 1M invocations + 400K GB-sec free forever (you'll use ~3% of that)
- DynamoDB: 25 GB storage + 25 WCU/RCU free forever (you'll use trivially less)
- CloudWatch Logs: 5 GB ingestion free, 50 MB retention free

The only way to incur cost: send 100x more often than the default 30 seconds,
or accidentally leave a load test running. Set the billing alarm described in
the main project notes.

## Tearing it all down

```bash
cd terraform
terraform destroy
```

Removes the Lambda, DynamoDB table, log groups, and IAM role.

## Hardcoded device ID
The ESP32 sends `device_id = "esp32poc001"` (hardcoded in the sketch).
Change later when you build more devices.
