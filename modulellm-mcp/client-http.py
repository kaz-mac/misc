#!/usr/bin/env python3
# Module-LLM LLM/TTSのMCPクライアント
# Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
# Released under the MIT license.

import sys
import asyncio
import argparse
from fastmcp import Client
from fastmcp.client.transports import SSETransport
import json

async def main(url):
    # サーバーを起動する Python 実行ファイルとスクリプトを指定
    #transport = PythonStdioTransport("weather.py")
    transport = SSETransport(url)

    async with Client(transport) as client:
        # 利用可能なツール一覧を取得
        tools = await client.list_tools()
        print("=== Available tools ===")
        for tool in tools:
            print(f"- {tool.name}: {tool.description}")
            print("  Parameters:")
            
            # ツール情報全体を表示（デバッグ用）
            if hasattr(tool, "inputSchema"):
                input_schema = tool.inputSchema
                if input_schema and "properties" in input_schema:
                    for param_name, param_info in input_schema["properties"].items():
                        param_type = param_info.get("type", "unknown")
                        param_desc = param_info.get("description", "No description")
                        print(f"    - {param_name} ({param_type}): {param_desc}")
                else:
                    print("    No parameter properties found in inputSchema")
            else:
                print("    No inputSchema attribute found")
            print()

        # send_message ツールを呼び出し
        print("\n=== Send Message ===")
        message = "こんにちは。あなたの名前は何ですか？"
        #message = "Hello. What's your name?"
        response = await client.call_tool(
            "send_message", 
            {"message": message}
        )
        print(response)

        # set_led_colors ツールを呼び出し
        print("\n=== SET LED COLORS ===")
        response = await client.call_tool(
            "set_led_colors", 
            {"red":0, "green":127, "blue":127}
        )
        print(response)

        # speak_text ツールを呼び出し
        print("\n=== Seak Text ===")
        message = "Hello, I am Stack chan. Nice to meet you."
        response = await client.call_tool(
            "speak_text", 
            {"message": message}
        )
        print(response)

        # set_led_colors ツールを呼び出し
        print("\n=== SET LED COLORS ===")
        response = await client.call_tool(
            "set_led_colors", 
            {"red":0, "green":127, "blue":0}
        )
        print(response)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='MCP Server debug tool')
    parser.add_argument('--url', type=str, default='http://127.0.0.1:8000/sse', 
                       help='URL of MCP server (default: http://127.0.0.1:8000/sse)')
    
    args = parser.parse_args()
    print(f"MCP Server URL: {args.url}")
    
    asyncio.run(main(args.url))
