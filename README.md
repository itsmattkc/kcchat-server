# KC Chat Backend

This is the server backend that runs the live chat at [mattkc.com/live](https://mattkc.com/live). It is written in C++ and relies heavily on Qt 5+ and MySQL/MariaDB.

## Building

The server should compile on any system with CMake and Qt 5+ available. It requires the following Qt modules: Core, Sql (MySQL/MariaDB), Network, and WebSockets. Once you have those installed through whatever means are available for your OS, building should be as simple as:

```
$ git clone https://github.com/itsmattkc/kcchat-server.git
$ cd kcchat-server
$ mkdir build
$ cd build
$ cmake ..
$ make
```

## Binaries (Ubuntu 20.04)

Binaries for Ubuntu 20.04 are available from the [Actions](https://github.com/itsmattkc/kcchat-server/actions) tab. To run on Ubuntu 20.04, you will need the following dependencies installed from `apt`: `libqt5core5a`, `libqt5network5`, `libqt5sql5-mysql`, `libqt5websockets5`.

## Quick Start

This assumes you already have the server compiled.

1. Install `doc/initial.sql` into a MySQL/MariaDB database. This can be done with the following command (assuming a database named `kcchat` has been created, this can be renamed to whatever you want):

```
mysql kcchat < initial.sql
```

2. Rename or copy `doc/config.json.sample` to `config.json` and place in the same directory as the executable. Open `config.json` in your preferred editor and start configuring it:

### Configuration (config.json)

1. Enter the MySQL/MariaDB details necessary for accessing your database into `db_host`, `db_port`, `db_user`, `db_pass`, and `db_name`.

2. Enter a valid Google cloud/developer client ID and secret into `youtube_client_id` and `youtube_client_secret`. This is currently the only option for authenticating users (though more will be added later).

3. Optionally, configure the bot's in-chat display name and color with `bot_name` and `bot_color`.

4. Optionally, set SSL keys/certificates with `ssl_key`, `ssl_crt`, and (optionally) `ssl_ca` to run the server on the secured `wss://` protocol rather than `ws://`. Many browsers require this if the server isn't running on `localhost`.

6. Optionally, enter a valid PayPal developer client ID/secret into `paypal_client_id` and `paypal_client_secret` to enable donations. For testing/development in [PayPal Sandbox](https://developer.paypal.com/tools/sandbox/), leave `paypal_live` set to `false` (Sandbox-specific client ID/secret will also be expected). To start accepting real monetary donations in production, set the client ID/secret to live (i.e. non-sandbox) credentials, and set `paypal_live` to `true`.

7. Optionally, set a timezone to a valid IANA ID representing the timezone of the streamer. This is used to display the streamer's local time correctly with the `!time` command regardless of the server's timezone.

## Warning: Unstable

This code is still under development. The API/communication protocol is not currently considered stable and may change without warning. A stable release/protocol/API may be released at a later date.
