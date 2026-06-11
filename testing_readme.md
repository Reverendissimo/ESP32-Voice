# Testing README

## Purpose

This repository should ship with tests from the start.

Use a layered strategy:
- host tests for pure logic
- Unity target tests for component behavior on ESP-IDF
- pytest integration tests for board-level behavior
- manual smoke checklist for hardware-heavy flows

## Why this strategy

ESP-IDF supports Unity component tests, pytest-based target automation, and Linux host testing for isolated logic.[cite:564][cite:565][cite:566]

This project needs all three because:
- pure logic should be cheap to test
- ESP integration needs target coverage
- real board flows need serial/network validation

## Minimum CI expectation

At minimum, automate:
- host tests
- formatting/linting if added
- static compile

If hardware CI is not available, keep pytest tests runnable locally on a dev bench with one board.

## Rule

Do not merge significant new features without at least:
- unit tests for the new logic
- one integration or smoke path if hardware behavior is involved
