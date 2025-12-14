#!/bin/bash

# Hook to check if modified files exceed 1200 lines
# Returns a system message prompting refactoring when files are too large

# Read JSON input from stdin
input=$(cat)

# Extract file path from tool input
file_path=$(echo "$input" | jq -r '.tool_input.file_path // ""')

# Validate we have a file path
if [[ -z "$file_path" || "$file_path" == "null" ]]; then
  exit 0
fi

# Count lines in the file
if [[ -f "$file_path" ]]; then
  line_count=$(wc -l < "$file_path" | tr -d ' ')

  if (( line_count > 1200 )); then
    # Get just the filename for cleaner output
    filename=$(basename "$file_path")

    # Return structured feedback to trigger refactoring
    jq -n \
      --arg file "$filename" \
      --arg path "$file_path" \
      --arg lines "$line_count" \
      '{
        "decision": "block",
        "reason": "File \($file) has \($lines) lines (exceeds 1200 line limit). Please refactor this file into smaller components before continuing."
      }'
  fi
fi

exit 0
