# Discord Maid Bot

C++ と D++ で作成した Discord Bot です。

リマインダー、TODO、個人用チャットルーム、VC募集を扱います。応答文は「メイドらしさ」を残しつつ短めにしています。

## 主な機能

| 機能 | 内容 |
| --- | --- |
| TODO | TODOの追加、一覧、完了、削除、未完了TODOの定期通知 |
| リマインダー | 日時指定、一覧、取消、通知の確認、未確認時の再通知 |
| 個人部屋 | ユーザーごとに1つ専用テキストチャンネルを作成 |
| VC募集 | 募集メッセージ、参加ボタン、プライベートVC作成、自動削除 |
| `/hi` | ボタン操作用メニュー |
| `/clean` | 個人部屋内のBotメッセージを削除 |
| 挨拶返答 | あいさつにメイド風の返答 |

## コマンド

| コマンド | 内容 |
| --- | --- |
| `/hi` | 操作メニューを表示 |
| `/help` | コマンド一覧を表示 |
| `/ping` | `pong!` を返す動作確認 |
| `/room` | 自分専用のテキストチャンネルを作成 |
| `/clean` | 専用部屋内のBotメッセージを削除 |
| `/todo add task:...` | TODOを追加 |
| `/todo list` | TODO一覧を表示 |
| `/todo done id:...` | TODOを完了 |
| `/todo delete id:...` | TODOを削除 |
| `/todo notify interval:...` | 未完了TODOの通知間隔を設定 |
| `/remind add when:... message:...` | リマインダーを追加 |
| `/remind list` | リマインダー一覧を表示 |
| `/remind cancel` | リマインダーをボタンで取消 |
| `/remind help` | 時間指定の例を表示 |
| `/vc game:...` | 募集用プライベートVCを作成 |

## `/hi` メニュー

`/hi` を実行すると、TODO、リマインダー、部屋、募集のボタンメニューが表示されます。

メニューや入力待ちの指示は、基本的に実行した本人にだけ見える表示です。入力待ちをやめたい場合は、次のどれかを送信してください。

```text
キャンセル
cancel
中止
やめる
```

## VC募集

`/vc` で募集用のプライベートVCを作成できます。

例:

```text
/vc game:valorant user1:@相手 role:@ロール start:21:00 note:あと1人
```

指定できる項目:

| オプション | 内容 |
| --- | --- |
| `game` | ゲーム名。VC名にも使われます |
| `user1` `user2` `user3` | メンションするユーザー |
| `role` | メンションするロール |
| `start` | 開始時刻。例: `21:00`、`今から`、`未定` |
| `note` | 任意メッセージ |

`@everyone` は使いません。ユーザーやロールを指定した場合だけ、その相手にメンションします。何も指定しなければメンションなしで募集します。

募集には参加ボタンが付きます。参加ボタンを押したユーザーには、そのVCへ入る権限が付与されます。VCに誰もいない状態が10分続くと、自動で削除されます。

個人部屋で募集を作った場合、募集メッセージは全体向けチャンネルへ送られます。送信先は次の優先順です。

1. `.env` の `DISCORD_ANNOUNCE_CHANNEL_ID`
2. Discordサーバーのシステムチャンネル
3. コマンドを実行したチャンネル

## TODO

TODOはユーザーごとに分かれます。

`/todo notify` で未完了TODOの定期通知を設定できます。

指定できる間隔:

```text
off
30m
1h
1d
```

通知はユーザーの専用部屋に届きます。通知メッセージには `完了: TODO名` ボタンが付き、押すとそのTODOを完了できます。

## リマインダー

リマインダーはユーザーごとに分かれます。通知時は設定した本人だけにメンションします。

時間指定の例:

```text
10m
2h
1d
18:30
明日 9時
6/20 21:00
月曜 8時
```

通知文は次の形式です。

```text
ご主人様、○○のお時間です。
```

通知後、`✅` リアクションまたは確認ボタンを押すまで、5分ごとに再通知します。

## 個人部屋

`/room` でユーザーごとの専用テキストチャンネルを作成します。

仕様:

```text
ユーザー1人につき1部屋
部屋名は「ユーザー名の部屋」
一度作成した部屋は自動削除しない
Bot再起動後もSQLiteに保存された情報を使う
```

## ロールによる応答

`ご主人様` という名前のロールを持つユーザーには丁寧に返答します。

そのロールがないユーザーには、短く冷たい返答をします。

## データ保存

データはSQLiteに保存します。

```text
data/bot.sqlite
```

保存対象:

```text
TODO
リマインダー
TODO通知設定
ユーザー専用部屋
```

`.env`、`data/`、`build/`、ログファイルはGit管理から除外しています。

## 構成

```text
src/
  main.cpp
  bot/
    commands.hpp/.cpp
    hima_voice_manager.hpp/.cpp
    reminder_worker.hpp/.cpp
    room_manager.hpp/.cpp
  config/
    env_loader.hpp/.cpp
  domain/
    reminder.hpp
    todo.hpp
  storage/
    storage.hpp/.cpp
scripts/
  setup.py
  detect_guild.py
```

## ビルド

Ubuntu 24.04 の例です。

```bash
sudo apt-get update
sudo apt-get install -y cmake g++ wget ca-certificates libssl-dev libopus-dev libsqlite3-dev
wget -O /tmp/dpp.deb https://dl.dpp.dev/
sudo dpkg -i /tmp/dpp.deb || sudo apt-get install -f -y
```

```bash
cmake -S . -B build
cmake --build build
```

## 初期設定

初回はセットアップスクリプトを使えます。

```bash
python3 scripts/setup.py
```

手動で設定する場合は `.env.example` をコピーして `.env` を作ります。

```bash
cp .env.example .env
nano .env
```

例:

```text
DISCORD_BOT_TOKEN=your_bot_token
DISCORD_GUILD_ID=your_test_guild_id
DISCORD_ANNOUNCE_CHANNEL_ID=optional_announce_channel_id
```

## コマンド登録

スラッシュコマンドを登録します。

```bash
./build/discord_productivity_bot --register-commands
```

`DISCORD_GUILD_ID` を設定している場合は、そのサーバーへ即時反映されます。空にするとグローバルコマンドとして登録します。

## 起動

通常起動:

```bash
./build/discord_productivity_bot
```

バックグラウンド起動:

```bash
nohup ./build/discord_productivity_bot > bot.log 2>&1 &
```
