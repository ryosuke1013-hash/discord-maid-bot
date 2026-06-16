#!/usr/bin/env python3

import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path


def load_env(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.exists():
        return values

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip().strip("\"'")
    return values


def write_env_value(path: Path, key: str, value: str) -> None:
    lines = path.read_text(encoding="utf-8").splitlines() if path.exists() else []
    replaced = False
    next_lines: list[str] = []

    for line in lines:
        if line.startswith(f"{key}="):
            next_lines.append(f"{key}={value}")
            replaced = True
        else:
            next_lines.append(line)

    if not replaced:
        next_lines.append(f"{key}={value}")

    path.write_text("\n".join(next_lines) + "\n", encoding="utf-8")


def main() -> int:
    env_path = Path(".env")
    env = load_env(env_path)
    token = os.environ.get("DISCORD_BOT_TOKEN") or env.get("DISCORD_BOT_TOKEN")
    if not token:
        print("DISCORD_BOT_TOKEN is missing in .env")
        return 1

    request = urllib.request.Request(
        "https://discord.com/api/v10/users/@me/guilds",
        headers={
            "Authorization": f"Bot {token}",
            "User-Agent": "discord-productivity-bot setup",
        },
    )

    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            guilds = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        if error.code == 401:
            print("Discord rejected the token. Reset/copy the Bot Token again.")
            return 1
        raise

    if isinstance(guilds, dict) and "message" in guilds:
        print(f"Discord API error: {guilds.get('message')}")
        return 1

    print(f"Found {len(guilds)} server(s) for this bot.")
    for index, guild in enumerate(guilds, start=1):
        print(f"{index}. {guild.get('name', '(unknown)')} [{guild.get('id', '')}]")

    if len(guilds) == 1:
        guild_id = guilds[0]["id"]
        write_env_value(env_path, "DISCORD_GUILD_ID", guild_id)
        print("DISCORD_GUILD_ID was written to .env automatically.")
        return 0

    if len(guilds) == 0:
        print("The bot is not in any server, or the token is invalid.")
        return 1

    print("Multiple servers found. Put the target server ID in .env manually.")
    return 2


if __name__ == "__main__":
    sys.exit(main())
