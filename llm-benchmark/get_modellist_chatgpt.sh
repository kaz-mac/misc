#!/bin/bash

if [ -z "$OPENAI_API_KEY" ]; then
  echo "OPENAI_API_KEY is not defined"
  exit 1
fi

curl -sS "https://api.openai.com/v1/models" \
  -H "Authorization: Bearer ${OPENAI_API_KEY}" \
  | jq '.data[].id' | sort \
  > model_chatgpt.txt
