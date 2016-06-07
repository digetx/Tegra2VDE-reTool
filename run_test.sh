#!/bin/bash

# Set the paths here
LOGS_DIR="/home/dima/vl/logs/VDE_logs"
REMOTE_ROOT="/home/dima/vl/nfs_root/android"

DATE=$(date +"%d.%m_%H:%M:%S")
LOCK_FILE=$0
RUNS=0

prepare() {
	adb connect localhost || exit $?
}

filter() {
	cat "$1" > "$1.filtered"
	echo "$1.filtered"
}

process_log() {
	./bin_to_txt.pl "$1" "$2" || exit $?

	cp "$1" "$4/" || exit $?

	echo -e "$3\n" > "$2.processed"
	echo -e "$3\n" > "$4/dmesg.cleaned.txt"

	cat "$(filter $2)" >> "$2.processed"
	./split.pl "$2.filtered"

	perl -pe 's/^<\d>\[[ \d]+\.[\d ]+\] //g' "$4/dmesg.txt" >> "$4/dmesg.cleaned.txt"
	./split.pl "$4/dmesg.cleaned.txt"

	./mk_graph.pl "$2.processed" "$4/graph.png"
}

join_and_show_diff() {
	LOG_PROCESS_FAIL=0

	for job in `jobs -p`
	do
		wait $job || ((LOG_PROCESS_FAIL++))
	done

	[ "$LOG_PROCESS_FAIL" == "0" ] || exit $LOG_PROCESS_FAIL

	echo "running \`meld \"$LOGS_DIR/$DATE/\"*/io_trace*.txt.processed\`"
	echo "running \`meld \"$LOGS_DIR/$DATE/\"*/dmesg.cleaned.txt\`"

	meld "$LOGS_DIR/$DATE/"*/io_trace*.txt.processed &
	meld "$LOGS_DIR/$DATE/"*/dmesg.cleaned.txt &
}

run_test_on_remote() {
	cp "$1/test.mp4" "$REMOTE_ROOT/data/media/" || exit $?
	adb logcat -c
	adb shell busybox dmesg -c > /dev/null
	adb shell stagefright -r "/data/media/test.mp4"
	adb shell busybox dmesg -c > "$1/dmesg.txt"
	adb logcat -d > "$1/logcat.txt"
	cp "$REMOTE_ROOT/storage/sdcard0/out.jpg" "$1/"
}

collect_trace_log() {
	exec 123<$LOCK_FILE
	flock 123 || exit $?

	dbus-send --type=method_call --dest=org.traceviewer / org.traceviewer.control.stopRecordingCPU || exit $?
	dbus-send --type=method_call --dest=org.traceviewer / org.traceviewer.control.stopRecordingAVP || exit $?
	dbus-send --type=method_call --dest=org.traceviewer / org.traceviewer.control.setTimeSpeed boolean:false || exit $?

# 	dbus-send --type=method_call --dest=org.traceviewer / org.traceviewer.control.setTimeSpeed boolean:true || exit $?
	dbus-send --type=method_call --dest=org.traceviewer / org.traceviewer.control.startRecordingCPU || exit $?
	dbus-send --type=method_call --dest=org.traceviewer / org.traceviewer.control.startRecordingAVP || exit $?

	dbus-send --type=method_call --dest=org.traceviewer /CPU/car     local.trace_viewer.TraceDev.setRecording boolean:true || exit $?
	dbus-send --type=method_call --dest=org.traceviewer /CPU/dram    local.trace_viewer.TraceDev.setRecording boolean:true || exit $?
	dbus-send --type=method_call --dest=org.traceviewer /CPU/iram    local.trace_viewer.TraceDev.setRecording boolean:true || exit $?
	dbus-send --type=method_call --dest=org.traceviewer /AVP/ucq     local.trace_viewer.TraceDev.setRecording boolean:true || exit $?
	dbus-send --type=method_call --dest=org.traceviewer /AVP/bsea    local.trace_viewer.TraceDev.setRecording boolean:true || exit $?
	dbus-send --type=method_call --dest=org.traceviewer /AVP/sxe     local.trace_viewer.TraceDev.setRecording boolean:true || exit $?
	dbus-send --type=method_call --dest=org.traceviewer /AVP/bsev    local.trace_viewer.TraceDev.setRecording boolean:true || exit $?
	dbus-send --type=method_call --dest=org.traceviewer /AVP/mbe     local.trace_viewer.TraceDev.setRecording boolean:true || exit $?
	dbus-send --type=method_call --dest=org.traceviewer /AVP/ppe     local.trace_viewer.TraceDev.setRecording boolean:true || exit $?
	dbus-send --type=method_call --dest=org.traceviewer /AVP/mce     local.trace_viewer.TraceDev.setRecording boolean:true || exit $?
	dbus-send --type=method_call --dest=org.traceviewer /AVP/tfe     local.trace_viewer.TraceDev.setRecording boolean:true || exit $?
	dbus-send --type=method_call --dest=org.traceviewer /AVP/ppb     local.trace_viewer.TraceDev.setRecording boolean:true || exit $?
	dbus-send --type=method_call --dest=org.traceviewer /AVP/vdma    local.trace_viewer.TraceDev.setRecording boolean:true || exit $?
	dbus-send --type=method_call --dest=org.traceviewer /AVP/ucq     local.trace_viewer.TraceDev.setRecording boolean:true || exit $?
	dbus-send --type=method_call --dest=org.traceviewer /AVP/bsea2   local.trace_viewer.TraceDev.setRecording boolean:true || exit $?
	dbus-send --type=method_call --dest=org.traceviewer /AVP/frameid local.trace_viewer.TraceDev.setRecording boolean:true || exit $?
	dbus-send --type=method_call --dest=org.traceviewer /AVP/dram    local.trace_viewer.TraceDev.setRecording boolean:true || exit $?
	dbus-send --type=method_call --dest=org.traceviewer /AVP/iram    local.trace_viewer.TraceDev.setRecording boolean:true || exit $?

	run_test_on_remote "$1" 1>&2

	dbus-send --print-reply=literal --dest=org.traceviewer / org.traceviewer.control.recordingFilePath | sed -e 's/^[[:space:]]*//' || exit $?

	dbus-send --type=method_call --dest=org.traceviewer / org.traceviewer.control.stopRecordingCPU || exit $?
	dbus-send --type=method_call --dest=org.traceviewer / org.traceviewer.control.stopRecordingAVP || exit $?
	dbus-send --type=method_call --dest=org.traceviewer / org.traceviewer.control.setTimeSpeed boolean:false || exit $?

	flock -u 123 || exit $?
}

generate_test_file() {
	echo "$2" > "$1/params.txt"
	./h264_test_generator -o "$1/test.h264" -d "$1" $2 || exit $?
	ffmpeg -loglevel debug -r 5 -i "$1/test.h264" -vcodec copy -y "$1/test.mp4"
	mpv --speed=10 "$1/test.mp4"
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

join_and_show_diff
