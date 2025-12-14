#!/bin/bash

# Block edits that grow files over ~25000 tokens (Read tool limit)
# Code tokenizes at ~3.5 bytes/token, so 25000 tokens ~= 87KB. Use 80KB for safety margin.

MAX_BYTES=80000

# Extract file_path from JSON using grep/sed (avoids jq dependency)
input=$(cat)
file_path=$(echo "$input" | grep -o '"file_path"[[:space:]]*:[[:space:]]*"[^"]*"' | sed 's/.*:.*"\([^"]*\)"/\1/')
[[ -z "$file_path" || ! -f "$file_path" ]] && exit 0

# Get current file size in bytes
current=$(wc -c < "$file_path" | tr -d ' ')
(( current <= MAX_BYTES )) && exit 0

# Get previous size from git
previous=$(git show HEAD:"$file_path" 2>/dev/null | wc -c | tr -d ' ')
[[ -z "$previous" || "$previous" == "0" ]] && exit 0

if (( current > previous )); then
  filename=$(basename "$file_path")
  increase=$((current - previous))
  current_kb=$((current / 1024))
  echo "{\"decision\":\"block\",\"reason\":\"File $filename is ~${current_kb}KB (over 100KB token limit) and grew by $increase bytes. Please refactor into smaller components.\"}"
fi
