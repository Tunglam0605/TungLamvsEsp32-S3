# ADR-002: Layered architecture

The platform follows one-way dependencies: application, platform service, BSP,
driver, ESP-IDF. Phase 0-1 implements only application, BSP, and driver layers
needed for hardware diagnostics.
