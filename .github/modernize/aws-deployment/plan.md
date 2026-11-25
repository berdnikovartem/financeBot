# Modernization Plan: Deploy Finance Bot to AWS

**Project**: financeBot

## Technical Framework

- **Languages**: Java 21, C++17, TypeScript/JavaScript
- **Frameworks**: Spring Boot 4.1.0, React/Vite frontend
- **Build tools**: Maven Wrapper, CMake, npm
- **Database**: PostgreSQL 16 in the current Compose setup
- **Current deployment**: Docker Compose services for Java API, web frontend,
  C++ trading worker, and PostgreSQL
- **Initial target**: Cost-capped AWS demo deployment; exact AWS topology is
  pending feasibility review
- **Infrastructure as code**: Terraform

## Overview

This migration prepares the Finance Bot for an internet-accessible AWS demo
under a strict $10/month ceiling. The user selected Terraform, `us-west-2`,
and an AWS-managed CloudFront HTTPS hostname, and most recently chose
demo-only operation with real credentials and trading disabled. This
supersedes the earlier request for full real trading behavior and the
initial ECS Fargate/RDS target.

The original production-style target (ECS Fargate, private RDS, public
ingress) is not compatible with the budget based on the current rough
estimate. A smaller single-instance design may fit the budget but has
resource, availability, and durability limitations. The exact design must be
reviewed and approved before Terraform or AWS provisioning. Automated tests
will use TestContainers and mocks, never live exchange credentials, trades,
or Telegram alerts. AWS pricing and free-tier eligibility must be verified
before resources are created; this plan does not authorize provisioning.

The work is phased: capture the current behavior, define infrastructure,
prepare the application for cloud configuration, verify changes, run a
security/dependency review, and deploy only after those checks pass. This plan
does not authorize or perform AWS provisioning.

## Candidate Demo Architecture (Not Yet Approved)

- **Public entry**: AWS-managed CloudFront HTTPS hostname. AWS advertises a
  flat-rate plan starting at $0/month; plan limits and origin design must be
  verified before use.
- **Compute candidate**: One small Lightsail Linux VM running the web and API
  containers and a local PostgreSQL container. AWS currently lists $7/month
  for 1 GB RAM with IPv4; this would be a single point of failure with tight
  memory. The $10 IPv6-only plan lists 2 GB but may not provide the required
  reachability to all internet users. Test the actual configuration before
  selecting either.
- **Trading worker**: Do not enable live trading or accept real exchange
  credentials in the public demo. The worker may be omitted or kept stopped.
- **Database**: Demo-only local PostgreSQL storage; this is not an RDS
  production replacement and does not provide managed database durability.
- **Secrets and configuration**: Do not expose real user/exchange secrets.
  Keep any demo-only credentials outside source code and images.
- **Cost controls**: Stay within the $10/month cap, verify current regional
  prices and CloudFront plan eligibility, and avoid resources with separate
  hourly charges. Add alerts where available but do not assume alerts cap
  spending automatically.

AWS's official Lightsail pricing page currently lists Linux virtual-server
plans at $5/month for 0.5 GB RAM and $7/month for 1 GB with public IPv4; a
2-GB IPv4 plan is $12/month. These prices do not establish that the full
application fits safely on the smaller plans.
See [AWS Lightsail pricing](https://aws.amazon.com/lightsail/pricing/).

For the earlier real-use architecture, a rough preliminary estimate is
approximately **$60–$100+/month** for small always-on Fargate tasks, an
Application Load Balancer, single-AZ RDS, and low-traffic networking, before
taxes and variable usage. NAT Gateway alone can add about $33/month at the
published example rate. This is not a quote; exact US West (Oregon) task sizes,
account eligibility, and traffic must be entered in the
[AWS Pricing Calculator](https://calculator.aws/).

## Migration Impact Summary

| Component | Current | AWS target | Exposure |
|---|---|---|---|
| Web/API | Compose | Small AWS demo host TBD | CloudFront HTTPS |
| C++ worker | Compose | Stopped/omitted in demo | No trading |
| PostgreSQL | Compose | Demo-only local storage | Not public |
| Credentials | Local env | No real credentials in demo | Not accepted |
| Secrets | Local env | Demo-only protected config | Never public |

## Phases and Acceptance Gates

1. **Baseline**: Record existing build/test behavior and key web/API/worker
   flows before changes. Do not use real exchange accounts or place trades in
   automated verification.
2. **Infrastructure design**: Compare and test a minimal demo architecture
   against the $10 cap, memory limits, CloudFront HTTPS, and public reachability.
   Review Terraform and current cost estimates. Provision only after explicit
   approval.
3. **Application readiness**: Make runtime configuration portable to AWS,
   ensure production container startup is appropriate, remove development
   credentials/defaults from production paths, and configure the worker to use
   the deployed API address without exposing its service token.
4. **Verification**: Run applicable unit and integration checks with
   TestContainers and mocks. Confirm API/database behavior and worker
   configuration retrieval without invoking real trading or alerting services.
5. **Security gate**: Scan dependencies for known CVEs and remediate
   applicable findings; verify secrets, network boundaries, logs, and public
   access settings before deployment.
6. **Deployment and handoff**: Publish versioned demo images, deploy only the
   approved demo services, verify public HTTPS and that live credentials and
   trading are disabled, and document operation, rollback, and cost monitoring.

Deployment must not proceed if the database is publicly reachable, the worker
is exposed to inbound internet traffic, required secrets are present in source
or images, or the HTTPS endpoint decision is unresolved.

## Open Questions & Questionnaire

- [x] Q: Which AWS container platform should be used? → A: ECS Fargate was
  the initial choice; it is superseded by the latest $10/month demo-only scope.
- [x] Q: Which region should be used? → A: `us-west-2`.
- [x] Q: Should infrastructure be provisioned as part of the migration plan?
  → A: Yes; include AWS infrastructure as code and provisioning steps.
- [x] Q: Which infrastructure-as-code tool should be used? → A: Terraform.
- [x] Q: Which integration-testing approach should be planned? → A:
  TestContainers and mocks; do not call live exchange or Telegram services.
- [x] Q: What is the cost preference? → A: Make the deployment as cheap as
  possible while retaining production-safe defaults.
- [x] Q: What is the maximum monthly AWS budget? → A: $10/month.
- [x] Q: Is a custom domain available? → A: No domain selected; use an
  AWS-managed public URL if practical.
- [x] Q: Which public HTTPS URL/TLS approach should be used? → A: Use an
  AWS-managed CloudFront HTTPS hostname; verify plan eligibility, configuration,
  and total costs before provisioning.
- [x] Q: Should the public site be demo-only or retain the app's real
  behavior? → A: Demo-only under the $10/month cap; disable real exchange
  credentials and trading. This supersedes the earlier real-behavior selection.
- [ ] Before provisioning, confirm a demo topology that fits within $10/month,
  supports the intended public audience over HTTPS, and has enough memory for
  the selected demo services.
