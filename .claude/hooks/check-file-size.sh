#!/bin/bash

# Hook to block edits that increase file size when the file is over 1000 lines
# Allows refactoring (reducing lines) but blocks growth of large files

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
  current_lines=$(wc -l < "$file_path" | tr -d ' ')

  # Only check files over 1000 lines
  if (( current_lines > 1000 )); then
    # Get the previous line count from git (HEAD version)
    previous_lines=$(git show HEAD:"$file_path" 2>/dev/null | wc -l | tr -d ' ')

    # If file didn't exist before, previous_lines will be 0
    if [[ -z "$previous_lines" ]]; then
      previous_lines=0
    fi

    # Block if the file has grown (any increase)
    if (( current_lines > previous_lines )); then
      # Get just the filename for cleaner output
      filename=$(basename "$file_path")
      increase=$((current_lines - previous_lines))

      # Return structured feedback to trigger refactoring
      jq -n \
        --arg file "$filename" \
        --arg path "$file_path" \
        --arg prev "$previous_lines" \
        --arg curr "$current_lines" \
        --arg inc "$increase" \
        '{
          "decision": "block",
          "reason": "File \($file) is over 1000 lines (\($curr) lines) and has grown by \($inc) lines. Please refactor this file into smaller components before continuing."
        }'
    fi
  fi
fi

exit 0
