// SPDX-License-Identifier: GPL-3.0-or-later
package main

import (
	"bytes"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"io"
	"net/netip"
	"strings"
)

// Configuration travels only over the private stdin pipe, never argv or environment.
type configuration struct {
	Version     int    `json:"version"`
	Server      string `json:"server"`
	Port        int    `json:"port"`
	UUID        string `json:"uuid"`
	Security    string `json:"security"`
	ServerName  string `json:"server_name"`
	Fingerprint string `json:"fingerprint"`
	PublicKey   string `json:"public_key"`
	ShortID     string `json:"short_id"`
	Flow        string `json:"flow"`
}

var errConfiguration = errors.New("invalid configuration")

func decodeConfiguration(data []byte) (configuration, error) {
	var result configuration
	if len(data) > 16384 {
		return result, errConfiguration
	}
	// Reject duplicate keys as well as unknown keys and trailing JSON.
	tokens := json.NewDecoder(bytes.NewReader(data))
	start, err := tokens.Token()
	if err != nil || start != json.Delim('{') {
		return result, errConfiguration
	}
	seen := make(map[string]bool)
	for tokens.More() {
		key, err := tokens.Token()
		name, ok := key.(string)
		if err != nil || !ok || seen[name] {
			return result, errConfiguration
		}
		seen[name] = true
		var value json.RawMessage
		if tokens.Decode(&value) != nil {
			return result, errConfiguration
		}
	}
	if _, err := tokens.Token(); err != nil {
		return result, errConfiguration
	}
	if _, err := tokens.Token(); err != io.EOF {
		return result, errConfiguration
	}
	decoder := json.NewDecoder(bytes.NewReader(data))
	decoder.DisallowUnknownFields()
	if decoder.Decode(&result) != nil || !result.valid() {
		return configuration{}, errConfiguration
	}
	return result, nil
}

func validHost(host string) bool {
	if len(host) < 1 || len(host) > 253 {
		return false
	}
	if addr, err := netip.ParseAddr(host); err == nil {
		return addr.Zone() == ""
	}
	host = strings.TrimSuffix(host, ".")
	for _, label := range strings.Split(host, ".") {
		if len(label) == 0 || len(label) > 63 || label[0] == '-' || label[len(label)-1] == '-' {
			return false
		}
		for _, c := range label {
			if !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') {
				return false
			}
		}
	}
	return true
}

func (c configuration) valid() bool {
	if c.Version != 1 || !validHost(c.Server) || c.Port < 1 || c.Port > 65535 || !validHost(c.ServerName) {
		return false
	}
	if len(c.UUID) != 36 || c.UUID[8] != '-' || c.UUID[13] != '-' || c.UUID[18] != '-' || c.UUID[23] != '-' {
		return false
	}
	if decoded, err := hex.DecodeString(strings.ReplaceAll(c.UUID, "-", "")); err != nil || len(decoded) != 16 {
		return false
	}
	if c.Flow != "" && c.Flow != "xtls-rprx-vision" {
		return false
	}
	switch c.Fingerprint {
	case "", "chrome", "firefox", "safari", "edge", "ios", "android", "randomized":
	default:
		return false
	}
	switch c.Security {
	case "tls":
		return c.PublicKey == "" && c.ShortID == ""
	case "reality":
		key, err := base64.RawURLEncoding.DecodeString(c.PublicKey)
		if err != nil || len(key) != 32 || len(c.PublicKey) != 43 || len(c.ShortID) > 16 || len(c.ShortID)%2 != 0 {
			return false
		}
		_, err = hex.DecodeString(c.ShortID)
		return err == nil
	default:
		return false
	}
}
