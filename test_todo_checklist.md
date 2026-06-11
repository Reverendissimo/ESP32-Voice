# Test TODO Checklist for Cursor

## Immediate
- [ ] Create host test scaffolding
- [ ] Create Unity component test scaffolding
- [ ] Create pytest scaffolding
- [ ] Add manual smoke checklist to docs

## Config
- [ ] Test patch/apply/save/load/revert/reset
- [ ] Test secret masking
- [ ] Test invalid config rejection

## Identity and security
- [ ] Test hardware-derived device UID formatting
- [ ] Test wrong target rejection
- [ ] Test auth-required endpoints reject missing token

## Platform
- [ ] Test health endpoint
- [ ] Test metrics endpoint
- [ ] Test version endpoint
- [ ] Test time trusted/untrusted state

## Audio/display
- [ ] Test playback payload validation
- [ ] Test display payload validation
- [ ] Test async path request correlation fields

## Sensors and alarms
- [ ] Test battery threshold transitions
- [ ] Test temperature/humidity threshold transitions
- [ ] Test hysteresis / clear threshold behavior
- [ ] Test cooldown behavior

## IR
- [ ] Test learn start validation
- [ ] Test learn timeout event
- [ ] Test send validation

## CLI
- [ ] Test help
- [ ] Test status
- [ ] Test wifi_test
- [ ] Test config_save
- [ ] Test reboot command handling
