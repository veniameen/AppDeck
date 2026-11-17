// SPDX-License-Identifier: GPL-3.0-or-later
package main

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"syscall"
	"time"
)

type lineResult struct {
	text string
	err  error
}

func lines(reader io.Reader) <-chan lineResult {
	result := make(chan lineResult, 1)
	go func() {
		defer close(result)
		scanner := bufio.NewScanner(reader)
		scanner.Buffer(make([]byte, 1024), 16385)
		for scanner.Scan() {
			result <- lineResult{text: scanner.Text()}
		}
		if err := scanner.Err(); err != nil {
			result <- lineResult{err: err}
		}
	}()
	return result
}

func run(input io.Reader, output io.Writer, args []string) int {
	mode := "serve"
	if len(args) == 1 && args[0] == "serve" {
		mode = "serve"
	} else if len(args) == 1 && args[0] == "check" {
		mode = "check"
	} else if len(args) != 0 {
		fmt.Fprintln(output, "fail 0 arguments")
		return 1
	}
	inputLines := lines(input)
	var first lineResult
	select {
	case value, ok := <-inputLines:
		if !ok || value.err != nil {
			fmt.Fprintln(output, "fail 0 configuration")
			return 1
		}
		first = value
	case <-time.After(30 * time.Second):
		fmt.Fprintln(output, "fail 0 configuration")
		return 1
	}
	config, err := decodeConfiguration([]byte(first.text))
	first.text = ""
	if err != nil {
		fmt.Fprintln(output, "fail 0 configuration")
		return 1
	}
	b, err := newBridge(config, nil)
	config = configuration{}
	if err != nil {
		fmt.Fprintln(output, "fail 0 initialization")
		return 1
	}
	defer b.close()
	if mode == "check" {
		start := time.Now()
		ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
		defer cancel()
		if b.check(ctx, "api.openai.com:443", nil) != nil {
			fmt.Fprintln(output, "fail 0 connection")
			return 1
		}
		fmt.Fprintf(output, "ok 0 %d\n", time.Since(start).Milliseconds())
		return 0
	}
	if _, err := fmt.Fprintf(output, "port %d\n", b.port()); err != nil {
		return 1
	}
	signals := make(chan os.Signal, 1)
	signal.Notify(signals, os.Interrupt, syscall.SIGTERM)
	defer signal.Stop(signals)
	watchTimer := time.NewTimer(60 * time.Second)
	defer watchTimer.Stop()
	ticker := time.NewTicker(500 * time.Millisecond)
	defer ticker.Stop()
	var watched int
	var birth uint64
	for {
		select {
		case <-signals:
			return 0
		case <-watchTimer.C:
			if watched == 0 {
				return 1
			}
		case <-ticker.C:
			if watched != 0 && processBirth(watched) != birth {
				return 0
			}
		case line, open := <-inputLines:
			if !open {
				if watched == 0 {
					return 0
				}
				inputLines = nil
				continue
			}
			if line.err != nil {
				return 1
			}
			fields := strings.Fields(line.text)
			if len(fields) != 2 || fields[0] != "watch" || watched != 0 {
				return 1
			}
			pid, err := strconv.Atoi(fields[1])
			if err != nil || pid <= 1 || pid > 2147483647 {
				return 1
			}
			birth = processBirth(pid)
			if birth == 0 {
				return 1
			}
			watched = pid
			watchTimer.Stop()
		}
	}
}

func safeRun() (code int) {
	defer func() {
		if recover() != nil {
			fmt.Fprintln(os.Stdout, "fail 0 internal")
			code = 1
		}
	}()
	return run(os.Stdin, os.Stdout, os.Args[1:])
}

func main() { os.Exit(safeRun()) }
