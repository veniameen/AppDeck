# AppDeck — native macOS profile manager. Everything runs on macOS with the Command Line Tools.
.PHONY: all app test test-claude-sessions test-claude-profiles test-claude-network test-usage-errors test-usage-cache test-claude-connection-ui verify install clean help

all: app

app:        ## Build build/AppDeck.app (universal, ad-hoc signed)
	@scripts/build.sh

test:       ## Portable policy tests (ASan/UBSan) and the localization check
	@scripts/test.sh

test-claude-sessions: ## Offline macOS Claude session fixtures for both architectures; no apps launched
	@bash scripts/test-claude-sessions.sh

test-claude-profiles: ## Native clean-profile launch fixtures for both architectures; no real Claude
	@bash scripts/test-claude-profiles.sh

test-claude-network: ## Linked browser and real proxy bridge fixtures; no vendor app or upstream requests
	@bash scripts/test-claude-network.sh

test-usage-errors: ## Synthetic vendor-error fixtures for both architectures; no account or network access
	@bash scripts/test-usage-errors.sh

test-usage-cache: ## Confirmed-account quota reuse against synthetic CLI fixtures
	@bash scripts/test-usage-cache.sh

test-claude-connection-ui: ## Synthetic form interactions and EN/RU renders; no profile writes
	@bash scripts/test-claude-connection-ui.sh

verify:     ## Full acceptance on this Mac: build, tests, native sync fixtures, API/AX smoke tests
	@scripts/verify.sh

install: app ## Build and copy AppDeck.app to /Applications (quit AppDeck first)
	@scripts/install.sh

clean:      ## Remove build outputs
	rm -rf build

help:       ## List the targets
	@grep -E '^[a-z]+:.*## ' $(MAKEFILE_LIST) | sed -E 's/:.*## /\t/'
