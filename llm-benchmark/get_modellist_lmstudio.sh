#!/bin/bash

curl -sS "http://127.0.0.1:1234/v1/models" \
  | jq '.data[].id' | sort \
  > model_lmstudio.txt
