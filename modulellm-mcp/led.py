#!/usr/bin/env python3
import asyncio
from typing import Any
import httpx
from mcp.server.fastmcp import FastMCP

# Initialize FastMCP server
mcp = FastMCP(
    "led",
    host="0.0.0.0",
    port=8000,
)

# LEDの明るさを設定する
def set_led_brightness(color: str, value: int) -> None:
    with open(f"/sys/class/leds/{color}/brightness", "w") as f:
        f.write(str(value))

# MCP Tool: LEDの明るさを設定する
@mcp.tool()
async def set_led_colors(red: int, green: int, blue: int) -> str:
    """Change the LED colors. (0-255)"""
    if (red < 0 or red > 255 or green < 0 or green > 255 or blue < 0 or blue > 255):
        return "Invalid color values. Please provide values between 0 and 255."
    
    try:
        set_led_brightness("R", red)
        set_led_brightness("G", green)
        set_led_brightness("B", blue)
        return "\n---\nsuccess"
    except Exception as e:
        return f"\n---\nerror: {str(e)}"


if __name__ == "__main__":
    # Run SSE over HTTP on port 8000
    asyncio.run(
        mcp.run_sse_async()
    )

