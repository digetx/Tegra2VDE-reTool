#!/bin/bash

# Set the paths here
LOGS_DIR="/VDE/logs/output/dir"
REMOTE_ROOT="/android/nfs/rootfs/path"

DATE=$(date +"%d.%m_%H:%M:%S")
LOCK_FILE=$(mktemp)
RUNS=0

prepare() {
	adb connect localhost || exit $?
}

cleanup() {
	rm $LOCK_FILE
}

filter() {
	cat "$1" | grep --invert-match DRAM\" > "$1.filtered"
	echo "$1.filtered"
}

process_log() {
	./bin_to_txt.pl "$1" "$2" || exit $?
	echo -e "$3\n" > "$2.processed"
	cat "$(filter $2)" >> "$2.processed"
	cp "$1" "$4/" || exit $?
}

join_and_run_meld() {
	LOG_PROCESS_FAIL=0

	for job in `jobs -p`
	do
		wait $job || ((LOG_PROCESS_FAIL++))
	done

	[ "$LOG_PROCESS_FAIL" == "0" ] || exit $LOG_PROCESS_FAIL

	meld "$LOGS_DIR/$DATE/"*/io_trace*.txt.processed &
}

run_test_on_remote() {
	cp "$1/test.mp4" "$REMOTE_ROOT/data/media/" || exit $?
	adb logcat -c
	adb shell stagefright -r -t "/data/media/test.mp4" 1>&2
	adb logcat -d > "$1/logcat.txt"
	cp "$REMOTE_ROOT/storage/sdcard0/out.jpg" "$1/"
}

collect_trace_log() {
	exec 123<$LOCK_FILE
	flock 123 || exit $?

	dbus-send --type=method_call --dest=org.traceviewer / org.traceviewer.control.startRecordingCPU || exit $?
	dbus-send --type=method_call --dest=org.traceviewer / org.traceviewer.control.startRecordingAVP || exit $?

	run_test_on_remote "$1"

	dbus-send --print-reply=literal --dest=org.traceviewer / org.traceviewer.control.recordingFilePath | sed -e 's/^[[:space:]]*//' || exit $?

	dbus-send --type=method_call --dest=org.traceviewer / org.traceviewer.control.stopRecordingCPU || exit $?
	dbus-send --type=method_call --dest=org.traceviewer / org.traceviewer.control.stopRecordingAVP || exit $?

	flock -u 123 || exit $?
}

generate_test_file() {
	echo "$2" > "$1/params.txt"
	./h264_test_generator -o "$1/test.h264" -d "$1" $2 || exit $?
	ffmpeg -loglevel debug -r 5 -i "$1/test.h264" -vcodec copy -y "$1/test.mp4" || exit $?
	mpv --speed=10 "$1/test.mp4" || exit $?
}

run_it() {
	generate_test_file "$1" "$2"
	process_log "$(collect_trace_log "$1")" "$1/io_trace$3.txt" "$2" "$1"
}

run_test() {
	SDIR="$LOGS_DIR/$DATE/$RUNS"
	mkdir -p "$SDIR" || exit $?
	run_it "$SDIR" "$1" "$RUNS" &
	(((RUNS++)))
}

prepare

# Add tests here:
#	run_test "--abc=d"
#	run_test "--abc=e"

join_and_run_meld

cleanup
