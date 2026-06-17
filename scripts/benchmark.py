"""Nexus MCP Server — Performance Benchmark Script.

Measures p50/p95/p99 latency for 10 representative commands spanning
query, mutation, and heavy operations. Run before and after each
architecture phase to track regressions.

Usage:
    uv run python scripts/benchmark.py [--host 127.0.0.1] [--port 13377] [--runs 20]

Requires the Nexus C++ plugin running in Unreal Engine.
"""

from __future__ import annotations

import argparse
import asyncio
import statistics
import sys
import time
from dataclasses import dataclass, field
from typing import Any

# Allow running from repo root
sys.path.insert(0, "src")

from nexus.connection.native import NativeTransport  # noqa: E402
from nexus.models.commands import CommandResult  # noqa: E402


@dataclass
class BenchmarkResult:
    command: str
    latencies_ms: list[float] = field(default_factory=list)
    errors: int = 0

    @property
    def p50(self) -> float:
        if not self.latencies_ms:
            return 0.0
        sorted_l = sorted(self.latencies_ms)
        idx = int(len(sorted_l) * 0.50)
        return sorted_l[min(idx, len(sorted_l) - 1)]

    @property
    def p95(self) -> float:
        if not self.latencies_ms:
            return 0.0
        sorted_l = sorted(self.latencies_ms)
        idx = int(len(sorted_l) * 0.95)
        return sorted_l[min(idx, len(sorted_l) - 1)]

    @property
    def p99(self) -> float:
        if not self.latencies_ms:
            return 0.0
        sorted_l = sorted(self.latencies_ms)
        idx = int(len(sorted_l) * 0.99)
        return sorted_l[min(idx, len(sorted_l) - 1)]

    @property
    def mean(self) -> float:
        return statistics.mean(self.latencies_ms) if self.latencies_ms else 0.0


# Representative commands covering different timeout tiers and subsystems
BENCHMARK_COMMANDS: list[tuple[str, dict[str, Any] | None]] = [
    # Tier: QUERY (read-only, fast)
    ("system.echo", {"ping": True}),
    ("editor.get_viewport_info", None),
    ("editor.get_world_info", None),
    ("actor.list_all", {"limit": 50, "offset": 0}),
    ("actor.find", {"query": "*", "limit": 10}),
    # Tier: MUTATION (spawn/modify)
    (
        "actor.spawn",
        {
            "actor_class": "StaticMeshActor",
            "label": "__benchmark_actor__",
            "location": {"x": 0, "y": 0, "z": -10000},
            "rotation": {"pitch": 0, "yaw": 0, "roll": 0},
            "scale": {"x": 1, "y": 1, "z": 1},
        },
    ),
    # Tier: QUERY (asset search)
    ("asset.search", {"query": "Cube", "limit": 5}),
    # Tier: QUERY (python paths)
    ("python.get_paths", None),
    # Tier: MUTATION (editor)
    ("editor.get_selection", None),
    # Tier: MUTATION (console command — lightweight)
    ("editor.execute_console_command", {"command": "stat none"}),
]


async def run_single(
    transport: NativeTransport,
    command: str,
    params: dict[str, Any] | None,
) -> tuple[float, bool]:
    """Run a single command, return (latency_ms, success)."""
    t0 = time.perf_counter()
    try:
        result: CommandResult = await transport.execute(command, params, timeout=30.0)
        elapsed = (time.perf_counter() - t0) * 1000
        return elapsed, result.success
    except Exception:
        elapsed = (time.perf_counter() - t0) * 1000
        return elapsed, False


async def cleanup_benchmark_actors(transport: NativeTransport) -> None:
    """Delete any actors spawned during benchmarking."""
    try:
        await transport.execute(
            "actor.delete_batch",
            {"actor_label_pattern": "__benchmark_actor__*"},
            timeout=10.0,
        )
    except Exception:
        pass


async def run_benchmark(host: str, port: int, runs: int) -> list[BenchmarkResult]:
    transport = NativeTransport(host, port)
    try:
        await transport.connect()
    except ConnectionError as exc:
        print(f"ERROR: Cannot connect to Nexus C++ plugin at {host}:{port}: {exc}")
        print("Ensure Unreal Engine is running with the Nexus plugin loaded.")
        sys.exit(1)

    print(f"Connected to Nexus C++ plugin at {host}:{port}")
    print(f"Running {runs} iterations per command ({len(BENCHMARK_COMMANDS)} commands)\n")

    results: list[BenchmarkResult] = []

    # Warmup: 2 iterations of echo
    for _ in range(2):
        await run_single(transport, "system.echo", {"ping": True})

    for command, params in BENCHMARK_COMMANDS:
        br = BenchmarkResult(command=command)
        for _ in range(runs):
            latency, success = await run_single(transport, command, params)
            if success:
                br.latencies_ms.append(latency)
            else:
                br.errors += 1
        results.append(br)

    # Cleanup benchmark actors
    await cleanup_benchmark_actors(transport)
    await transport.disconnect()

    return results


def print_results(results: list[BenchmarkResult]) -> None:
    header = f"{'Command':<45} {'p50':>8} {'p95':>8} {'p99':>8} {'mean':>8} {'err':>5}"
    print(header)
    print("-" * len(header))
    for r in results:
        print(
            f"{r.command:<45} {r.p50:>7.1f}ms {r.p95:>7.1f}ms "
            f"{r.p99:>7.1f}ms {r.mean:>7.1f}ms {r.errors:>4}e"
        )
    print()

    # Summary
    all_latencies = [lat for r in results for lat in r.latencies_ms]
    total_errors = sum(r.errors for r in results)
    if all_latencies:
        print(f"Overall: {len(all_latencies)} successful / {total_errors} errors")
        print(f"  p50={sorted(all_latencies)[len(all_latencies)//2]:.1f}ms")
        print(f"  mean={statistics.mean(all_latencies):.1f}ms")
    else:
        print("No successful commands!")


def main() -> None:
    parser = argparse.ArgumentParser(description="Nexus MCP Server Benchmark")
    parser.add_argument("--host", default="127.0.0.1", help="Nexus plugin host")
    parser.add_argument("--port", type=int, default=13377, help="Nexus plugin port")
    parser.add_argument("--runs", type=int, default=20, help="Iterations per command")
    args = parser.parse_args()

    results = asyncio.run(run_benchmark(args.host, args.port, args.runs))
    print_results(results)


if __name__ == "__main__":
    main()
