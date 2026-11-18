#!/usr/bin/env bash
# usage: run-phases.sh <phase> [phase...]   e.g. run-phases.sh 1 2 3 or G
R=D:/SJCM/01_SemiconductorStudio; A=$R/SST-Experiments-gui-bridge/scaps-agent; G=$R/SST-Experiments-gui-bridge/gui-bridge
V="$R/Semiconductor-Studio/Semiconductor-Studio-Vault/60_Research/Simulations/SCAPS-Agent"
for n in "$@"; do
  mkdir -p "$A/runs/phase-$n"; cat "$A/AGENT.md" "$A/phases/$n.md" > "$A/runs/phase-$n/prompt.md"
  bash $R/tools/agents/codex-job.sh "scaps-p$n" workspace-write "$A" "$A/runs/phase-$n/prompt.md" "$V/phase-$n-results.md" "Agent SCAPS faza $n" \
    -c "mcp_servers.gui.command='$G/.venv/Scripts/python.exe'" -c "mcp_servers.gui.args=['$G/server.py']" \
    -c mcp_servers.gui.startup_timeout_sec=60 -c mcp_servers.gui.tool_timeout_sec=180 \
    -c 'mcp_servers.gui.default_tools_approval_mode="approve"' || { echo "phase $n failed"; break; }
  cp "$A/annoyances.md" "$V/annoyances.md"; cp "$A/knowledge.md" "$V/knowledge.md"
done
