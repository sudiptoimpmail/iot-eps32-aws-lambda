# =============================================================================
# Outputs — shown after `terraform apply` completes
# =============================================================================

output "lambda_function_url" {
  description = "POST telemetry JSON to this URL from the ESP32"
  value       = aws_lambda_function_url.telemetry.function_url
}

output "dynamodb_table_name" {
  description = "DynamoDB table where rows land"
  value       = aws_dynamodb_table.telemetry.name
}

output "cloudwatch_log_group" {
  description = "Where Lambda writes its logs"
  value       = aws_cloudwatch_log_group.lambda_logs.name
}

output "test_curl_command" {
  description = "Quick test from your laptop"
  value       = "curl -X POST ${aws_lambda_function_url.telemetry.function_url} -H 'Content-Type: application/json' -d '{\"device_id\":\"esp32poc001\",\"temp\":22.5,\"humidity\":45,\"ts\":1000}'"
}
