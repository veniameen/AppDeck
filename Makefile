# AppDeck — native macOS profile manager. Everything runs on macOS with the Command Line Tools.
.PHONY: all app test verify install clean help

all: app

app:        ## Build build/AppDeck.app (universal, ad-hoc signed)
	@scripts/build.sh

test:       ## Portable policy tests (ASan/UBSan) and the localization check
	@scripts/test.sh

verify:     ## Full acceptance on this Mac: build, tests, native sync fixtures, API/AX smoke tests
	@scripts/verify.sh

install: app ## Build and copy AppDeck.app to /Applications (quit AppDeck first)
	@scripts/install.sh

clean:      ## Remove build outputs
	rm -rf build

help:       ## List the targets
	@grep -E '^[a-z]+:.*## ' $(MAKEFILE_LIST) | sed -E 's/:.*## /\t/'
