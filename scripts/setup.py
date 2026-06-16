#!/usr/bin/env python3

import getpass
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENV_PATH = ROOT / ".env"


def load_env() -> dict[str, str]:
    values: dict[str, str] = {}
    if not ENV_PATH.exists():
        return values

    for raw_line in ENV_PATH.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip().strip("\"'")
    return values


def write_env(values: dict[str, str]) -> None:
    lines: list[str] = []
    existing = ENV_PATH.read_text(encoding="utf-8").splitlines() if ENV_PATH.exists() else []
    written: set[str] = set()

    for line in existing:
        if "=" not in line or line.strip().startswith("#"):
            lines.append(line)
            continue

        key = line.split("=", 1)[0].strip()
        if key in values:
            lines.append(f"{key}={values[key]}")
            written.add(key)
        else:
            lines.append(line)

    for key, value in values.items():
        if key not in written:
            lines.append(f"{key}={value}")

    ENV_PATH.write_text("\n".join(lines) + "\n", encoding="utf-8")


def token_is_placeholder(token: str) -> bool:
    return token in {"", "your_bot_token_here", "your_bot_token", "あなたのBot Token"}


def fetch_guilds(token: str) -> list[dict[str, str]]:
    request = urllib.request.Request(
        "https://discord.com/api/v10/users/@me/guilds",
        headers={
            "Authorization": f"Bot {token}",
            "User-Agent": "discord-productivity-bot setup",
        },
    )

    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            data = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        if error.code == 401:
            raise RuntimeError("Discord rejected the token. Reset/copy the Bot Token again.") from error
        raise

    if not isinstance(data, list):
        raise RuntimeError(f"Unexpected Discord API response: {data}")
    return data


def choose_guild(guilds: list[dict[str, str]]) -> str:
    if not guilds:
        raise RuntimeError("This bot is not in any server yet.")

    print(f"Found {len(guilds)} server(s):")
    for index, guild in enumerate(guilds, start=1):
        print(f"  {index}. {guild.get('name', '(unknown)')} [{guild.get('id', '')}]")

    if len(guilds) == 1:
        return guilds[0]["id"]

    while True:
        selected = input("Choose the server number to use for development: ").strip()
        if selected.isdigit() and 1 <= int(selected) <= len(guilds):
            return guilds[int(selected) - 1]["id"]
        print("Please enter one of the listed numbers.")


def run(command: list[str], timeout: int | None = None) -> None:
    print("+ " + " ".join(command))
    subprocess.run(command, cwd=ROOT, check=True, timeout=timeout)


def register_commands() -> None:
    binary = ROOT / "build" / "discord_productivity_bot"
    print("+ ./build/discord_productivity_bot --register-commands")
    process = subprocess.Popen(
        [str(binary), "--register-commands"],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )

    deadline = time.time() + 30
    saw_registration = False
    try:
        while time.time() < deadline:
            line = process.stdout.readline() if process.stdout else ""
            if line:
                print(line, end="")
                if "Registered guild commands" in line or "Registered global commands" in line:
                    saw_registration = True
                    time.sleep(3)
                    break
            elif process.poll() is not None:
                break
            else:
                time.sleep(0.2)
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()

    if not saw_registration:
        print("Command registration process was stopped after waiting.")
        print("If commands do not appear in Discord, run this setup again.")


def main() -> int:
    env = load_env()
    token = os.environ.get("DISCORD_BOT_TOKEN") or env.get("DISCORD_BOT_TOKEN", "")

    if token_is_placeholder(token):
        print("Paste your Discord Bot Token. Input is hidden.")
        token = getpass.getpass("Bot Token: ").strip()
        if token_is_placeholder(token):
            print("No valid token was entered.")
            return 1

    guild_id = env.get("DISCORD_GUILD_ID", "")
    if not guild_id:
        guilds = fetch_guilds(token)
        guild_id = choose_guild(guilds)

    write_env({
        "DISCORD_BOT_TOKEN": token,
        "DISCORD_GUILD_ID": guild_id,
    })

    run(["cmake", "-S", ".", "-B", "build"])
    run(["cmake", "--build", "build", "-j2"])
    register_commands()

    print("")
    print("Setup done.")
    print("Start the bot with:")
    print("  ./build/discord_productivity_bot")
    return 0


if __name__ == "__main__":
    sys.exit(main())
