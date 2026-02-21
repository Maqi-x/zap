# Memory Management

Zap uses **Automatic Reference Counting (ARC)** instead of a Garbage Collector (GC) to manage its memory.

## Key Concepts
- **Automatic**: Memory is automatically allocated and deallocated.
- **Deterministic**: Objects are destroyed as soon as they are no longer referenced.
- **Lower Overhead**: No periodic garbage collection pauses mean better predictability and performance.

## ARC Benefits
- Smaller memory footprint compared to GC-based languages.
- Faster cold starts and consistent performance.
- Predictable behavior in real-time or low-latency systems.


