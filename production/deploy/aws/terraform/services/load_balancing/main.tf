/**
 * Copyright 2022 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

resource "aws_lb" "alb" {
  name               = "${var.service}-${var.environment}-${var.operator}-alb"
  internal           = var.internal
  load_balancer_type = "application"
  subnets            = var.elb_subnet_ids
  enable_http2       = true

  security_groups = [
    var.elb_security_group_id
  ]

  tags = {
    Name        = "${var.service}-${var.environment}-${var.operator}-alb"
    operator    = var.operator
    environment = var.environment
    service     = var.service
  }
}

resource "aws_route53_record" "alb_alias_record" {
  name    = "${var.service}-${var.environment}.${var.root_domain}"
  type    = "A"
  zone_id = var.root_domain_zone_id

  # The identifier makes the policy specific to domain name and the region that it is in.
  set_identifier = "${var.service}-${var.environment}-${var.root_domain}-${var.region}-alb-alias-record"

  # When DNS query for record is performed, Route53 evaluates the lowest
  # latency resource, and returns the correct region's IP address.
  # AWS documentation: https://docs.aws.amazon.com/Route53/latest/DeveloperGuide/routing-policy-latency.html
  latency_routing_policy {
    region = var.region
  }

  alias {
    evaluate_target_health = false
    name                   = aws_lb.alb.dns_name
    zone_id                = aws_lb.alb.zone_id
  }
}

resource "aws_lb_listener" "alb_listener" {
  load_balancer_arn = aws_lb.alb.arn
  port              = 443
  protocol          = "HTTPS"
  certificate_arn   = var.certificate_arn

  # Traffic that gets here cannot be handled
  default_action {
    type = "fixed-response"

    fixed_response {
      content_type = "text/plain"
      message_body = "Not implemented"
      status_code  = "501"
    }
  }
}

resource "aws_lb_target_group" "alb_grpc_target_group" {
  name                          = "${var.service}-${var.environment}-${var.operator}-alb-grpc-tg"
  port                          = var.server_port
  protocol                      = "HTTP"
  protocol_version              = "GRPC"
  vpc_id                        = var.vpc_id
  deregistration_delay          = 30
  load_balancing_algorithm_type = "least_outstanding_requests"


  health_check {
    protocol            = "HTTP"
    port                = var.server_port
    path                = var.grpc_healthcheck_path
    interval            = var.healthcheck_interval_sec
    healthy_threshold   = var.healthcheck_healthy_threshold
    unhealthy_threshold = var.healthcheck_unhealthy_threshold
    matcher             = "0"
  }

  tags = {
    Name        = "${var.service}-${var.environment}-${var.operator}-alb-grpc-tg"
    operator    = var.operator
    environment = var.environment
    service     = var.service
  }
}


resource "aws_lb_listener_rule" "alb_listener_grpc_rule" {
  listener_arn = aws_lb_listener.alb_listener.arn
  priority     = 2

  action {
    type             = "forward"
    target_group_arn = aws_lb_target_group.alb_grpc_target_group.arn
  }
  condition {
    path_pattern {
      values = var.grpc_api_paths
    }
  }
}
