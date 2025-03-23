#!/bin/sh

# Start the daemon
start_daemon() {
    echo "Starting aesdsocket..."
    start-stop-daemon -S -n aesdsocket -a /usr/bin/aesdsocket -- -d
    echo "aesdsocket started."
}

# Stop the daemon
stop_daemon() {
    echo "Stopping aesdsocket..."
    start-stop-daemon -K -n aesdsocket
    echo "aesdsocket stopped."
}

# Check for arguments
case "$1" in
    start)
        start_daemon
        ;;
    stop)
        stop_daemon
        ;;
    restart)
        stop_daemon
        start_daemon
        ;;
    *)
        echo "Usage: $0 {start|stop|restart}"
        exit 1
        ;;
esac
exit 0
