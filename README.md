# Discord Productivity Bot

A C++ Discord bot built with D++.

The first features are slash-command based reminders and a personal TODO list,
with command names and replies themed for "Goshujin-sama bot".

## Commands

| Command | Description |
| --- | --- |
| `/todo add task` | Add a TODO item |
| `/todo list` | List your TODO items with action buttons |
| `/todo done id` | Mark a TODO item as done |
| `/todo delete id` | Delete a TODO item |
| `/todo clear` | Delete completed TODO items |
| `/todo notify interval` | Notify unfinished TODO items in your private room |
| `/remind add when message` | Create a reminder, e.g. `10m`, `2h`, `1d` |
| `/remind list` | List your pending reminders with a cancel button |
| `/remind cancel id` | Cancel a reminder |
| `/remind help` | Show reminder time examples |
| `/hima game note` | Create a private voice channel for a game and post a join button. `note` is optional |
| `/room open` | Create a private room for the user |
| `/help` | Show all commands |

Private text rooms:

```text
/room open creates a private text channel for the user.
Each user gets one room.
Rooms are stored in SQLite and are not deleted automatically.
```

Hima voice rooms:

```text
/hima game:valorant note:あと1人 creates a private voice channel named after the game.
The command user can enter it immediately.
Users who press the join button are granted voice channel access.
If nobody is in the voice channel for 10 minutes, the bot deletes it.
Discord bots cannot force users who are not in voice to join automatically.
```

Reminder time examples:

```text
10m = after 10 minutes
2h  = after 2 hours
1d  = after 1 day
```

`/todo list` shows select menus for completing or deleting a specific task.
`/todo notify` can be set to `off`, `30m`, `1h`, or `1d`.
TODO notifications are sent to the user's private text room created by
`/room open`.
`/remind list` shows a select menu for cancelling a specific reminder.
Reminder notifications repeat every 5 minutes until the owner reacts with
`✅` or presses the confirmation button.

Data is stored in SQLite:

```text
data/bot.sqlite
```

If old `data/reminders.tsv` or `data/todos.tsv` files exist, they are migrated
into SQLite on first startup when the matching database table is empty.

## Structure

```text
src/
  main.cpp                   # Bot startup and dependency wiring
  bot/
    commands.hpp/.cpp        # Slash command definitions and handlers
    reminder_worker.hpp/.cpp # Background reminder delivery loop
  domain/
    reminder.hpp             # Reminder data model
    todo.hpp                 # TODO data model
  storage/
    storage.hpp/.cpp         # File persistence and thread-safe data access
```

`main.cpp` only wires the app together. The persistence details are hidden in
`Storage`, and Discord command handling is isolated in `bot/commands.cpp`.

## Build

Example using vcpkg on Windows:

```powershell
vcpkg install dpp:x64-windows
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

On Ubuntu, install D++ first, then run a normal CMake build.

This project was verified on Ubuntu 24.04 with:

```bash
sudo apt-get update
sudo apt-get install -y wget ca-certificates libssl-dev libopus-dev libsqlite3-dev
wget -O /tmp/dpp.deb https://dl.dpp.dev/
sudo dpkg -i /tmp/dpp.deb || sudo apt-get install -f -y
```

```bash
cmake -S . -B build
cmake --build build
```

## Run

For first-time setup, run:

```bash
python3 scripts/setup.py
```

The setup script asks for your Bot Token with hidden input, writes `.env`, detects
the server ID, builds the project, and registers slash commands.

Manual setup is also possible. Create a local `.env` file:

```bash
cp .env.example .env
nano .env
```

Example `.env`:

```text
DISCORD_BOT_TOKEN=your_bot_token
DISCORD_GUILD_ID=your_test_guild_id
```

Register slash commands:

```bash
./build/discord_productivity_bot --register-commands
```

Normal startup:

```bash
./build/discord_productivity_bot
```

Set `DISCORD_GUILD_ID` to register commands for one test server quickly. Leave it
empty to register global commands.
