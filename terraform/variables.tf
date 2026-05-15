# =============================================================================
# Variables — defaults shown here; override in terraform.tfvars or CLI flags
# =============================================================================

variable "aws_region" {
  description = "AWS region to deploy into"
  type        = string
  default     = "us-east-2"
}

variable "lambda_name" {
  description = "Name of the Lambda function"
  type        = string
  default     = "weather-iot-telemetry"
}

variable "table_name" {
  description = "Name of the DynamoDB table"
  type        = string
  default     = "weather-iot-telemetry"
}
