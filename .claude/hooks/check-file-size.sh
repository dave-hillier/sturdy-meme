#!/bin/bash

# Block edits that grow files over 1000 lines

# Extract file_path from JSON using grep/sed (avoids jq dependency)
input=$(cat)
file_path=$(echo "$input" | grep -o '"file_path"[[:space:]]*:[[:space:]]*"[^"]*"' | sed 's/.*:.*"\([^"]*\)"/\1/')
[[ -z "$file_path" || ! -f "$file_path" ]] && exit 0

current=$(wc -l < "$file_path" | tr -d ' ')
(( current <= 1000 )) && exit 0

previous=$(git show HEAD:"$file_path" 2>/dev/null | wc -l | tr -d ' ')
[[ -z "$previous" ]] && exit 0

if (( current > previous )); then
  filename=$(basename "$file_path")
  increase=$((current - previous))
  echo "{\"decision\":\"block\",\"reason\":\"File $filename is over 1000 lines ($current) and grew by $increase lines. Please refactor into smaller components.\"}"
fi
