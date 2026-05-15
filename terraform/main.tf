# =============================================================================
# Weather IoT — Terraform configuration
# =============================================================================
# Provisions:
#   - DynamoDB table for telemetry rows
#   - IAM role + policy for Lambda
#   - Lambda function (Python 3.12, arm64)
#   - Lambda Function URL (public HTTPS endpoint, no auth)
#   - CloudWatch log group for Lambda
# =============================================================================

terraform {
  required_version = ">= 1.5.0"
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
    archive = {
      source  = "hashicorp/archive"
      version = "~> 2.4"
    }
  }
}

provider "aws" {
  region = var.aws_region
}

# -----------------------------------------------------------------------------
# DynamoDB table
# -----------------------------------------------------------------------------
# Schema:
#   device_id (string, partition key)  — e.g. "esp32poc001"
#   ts        (number, sort key)        — epoch milliseconds (server-side)
#
# Rows look like:
#   { device_id: "esp32poc001", ts: 1715812345678,
#     temp_f: 72.5, humidity: 45, device_ms: 35420 }
#
# PAY_PER_REQUEST billing = no need to set capacity, charged per actual req.
# For this workload (~3000 writes/day), well within the free tier forever.
# -----------------------------------------------------------------------------
resource "aws_dynamodb_table" "telemetry" {
  name         = var.table_name
  billing_mode = "PAY_PER_REQUEST"
  hash_key     = "device_id"
  range_key    = "ts"

  attribute {
    name = "device_id"
    type = "S"
  }

  attribute {
    name = "ts"
    type = "N"
  }

  tags = {
    Project = "weather-iot"
  }
}

# -----------------------------------------------------------------------------
# IAM role for the Lambda function to assume
# -----------------------------------------------------------------------------
resource "aws_iam_role" "lambda_role" {
  name = "${var.lambda_name}-role"

  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect = "Allow"
      Principal = { Service = "lambda.amazonaws.com" }
      Action = "sts:AssumeRole"
    }]
  })
}

# -----------------------------------------------------------------------------
# IAM policy: write to DynamoDB + write to CloudWatch Logs
# -----------------------------------------------------------------------------
resource "aws_iam_role_policy" "lambda_policy" {
  name = "${var.lambda_name}-policy"
  role = aws_iam_role.lambda_role.id

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        # CloudWatch Logs (create log group, log stream, put events)
        Effect = "Allow"
        Action = [
          "logs:CreateLogGroup",
          "logs:CreateLogStream",
          "logs:PutLogEvents"
        ]
        Resource = "arn:aws:logs:*:*:*"
      },
      {
        # DynamoDB: only PutItem on the specific telemetry table
        Effect = "Allow"
        Action = [
          "dynamodb:PutItem"
        ]
        Resource = aws_dynamodb_table.telemetry.arn
      }
    ]
  })
}

# -----------------------------------------------------------------------------
# Package the Python handler into a zip Terraform can upload
# -----------------------------------------------------------------------------
data "archive_file" "lambda_zip" {
  type        = "zip"
  source_file = "${path.module}/../lambda/handler.py"
  output_path = "${path.module}/lambda.zip"
}

# -----------------------------------------------------------------------------
# CloudWatch log group for Lambda (explicit so we can set retention)
# -----------------------------------------------------------------------------
# Without this, Lambda auto-creates a log group with infinite retention,
# which slowly grows free-tier usage. 14 days is plenty for learning.
# -----------------------------------------------------------------------------
resource "aws_cloudwatch_log_group" "lambda_logs" {
  name              = "/aws/lambda/${var.lambda_name}"
  retention_in_days = 14
}

# -----------------------------------------------------------------------------
# Lambda function
# -----------------------------------------------------------------------------
resource "aws_lambda_function" "telemetry" {
  function_name = var.lambda_name
  role          = aws_iam_role.lambda_role.arn

  filename         = data.archive_file.lambda_zip.output_path
  source_code_hash = data.archive_file.lambda_zip.output_base64sha256

  handler       = "handler.lambda_handler"
  runtime       = "python3.12"
  architectures = ["arm64"]    # arm64 is ~20% cheaper per ms

  memory_size = 128            # default; plenty for this workload
  timeout     = 10             # seconds; DynamoDB writes are fast

  environment {
    variables = {
      TABLE_NAME = aws_dynamodb_table.telemetry.name
    }
  }

  # Make sure log group exists before the function (avoids race)
  depends_on = [aws_cloudwatch_log_group.lambda_logs]
}

# -----------------------------------------------------------------------------
# Lambda Function URL — public HTTPS endpoint, no auth
# -----------------------------------------------------------------------------
# AuthType NONE means anyone with the URL can POST. Fine for learning; treat
# the URL as a secret. Don't commit it to public git, etc.
# -----------------------------------------------------------------------------
resource "aws_lambda_function_url" "telemetry" {
  function_name      = aws_lambda_function.telemetry.function_name
  authorization_type = "NONE"

  cors {
    allow_origins = ["*"]
    allow_methods = ["POST"]
    allow_headers = ["content-type"]
  }
}

resource "aws_lambda_permission" "allow_public_invoke" {
  statement_id           = "FunctionURLAllowPublicAccess"
  action                 = "lambda:InvokeFunctionUrl"
  function_name          = aws_lambda_function.telemetry.function_name
  principal              = "*"
  function_url_auth_type = "NONE"
}

resource "aws_lambda_permission" "allow_public_invoke_function" {
  statement_id  = "AllowPublicInvokeFunction"
  action        = "lambda:InvokeFunction"
  function_name = aws_lambda_function.telemetry.function_name
  principal     = "*"
}