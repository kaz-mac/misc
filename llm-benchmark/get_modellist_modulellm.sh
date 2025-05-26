#!/bin/bash
uv run ./get_modellist_modulellm.py | sort \
  > model_modulellm.txt
