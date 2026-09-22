#!/usr/bin/env python3
"""Demo data for screenshots: fictional profiles with cached account limits.

    python3 tools/make_demo_data.py <folder>
    APPDECK_DATA_ROOT=<folder> build/AppDeck.app/Contents/MacOS/AppDeck -AppleLanguages '(en)'

Two apps (Codex in ChatGPT.app and Claude Desktop), six profiles with invented names and example.com
addresses, and limit numbers dated "now", so AppDeck shows them without asking any account for an
hour. The Claude card is the primary profile: if Claude Desktop is running, AppDeck attaches to it
as usual and shows it as running. Nothing is launched or signed in. The folder is DELETED first;
the real AppDeck folder is refused.
"""
import os, plistlib, shutil, sys, time, uuid

if len(sys.argv) != 2:
    sys.exit(__doc__)
root = os.path.abspath(sys.argv[1])
if 'Application Support/AppDeck' in root or root in ('/', os.path.expanduser('~')):
    sys.exit('refusing to touch ' + root)
shutil.rmtree(root, ignore_errors=True)
for d in ('Profiles', 'Shared'):
    os.makedirs(os.path.join(root, d), mode=0o700, exist_ok=True)
os.chmod(root, 0o700)

now = time.time()
HOUR, DAY = 3600, 86400


def uid():
    return str(uuid.uuid4()).upper()


def window(used, minutes, resets_in, label=None):
    w = {'used': float(used), 'minutes': minutes, 'resets': now + resets_in}
    if label:
        w['label'] = label
    return w


def usage(plan, email, *windows):
    return {'checked': now, 'windowsAt': now, 'plan': plan, 'email': email, 'kind': 'chatgpt', 'windows': list(windows)}


codex, claude = uid(), uid()
apps = [
    {'id': codex, 'name': 'ChatGPT', 'path': '/Applications/ChatGPT.app', 'bundleId': 'com.openai.codex', 'adapter': 'codex', 'usageLimits': True},
    {'id': claude, 'name': 'Claude', 'path': '/Applications/Claude.app', 'bundleId': 'com.anthropic.claudefordesktop', 'adapter': 'claude', 'usageLimits': True},
]
week, five = 7 * 24 * 60, 5 * 60
profiles = [
    {'appId': codex, 'name': 'Work', 'color': 0, 'share': True,
     'usage': usage('pro', 'alex@example.com', window(28, week, 3 * DAY + 5 * HOUR), window(12, five, 2 * HOUR))},
    {'appId': codex, 'name': 'Personal', 'color': 1, 'share': True,
     'usage': usage('plus', 'alex@example.org', window(59, week, 5 * DAY + 2 * HOUR), window(35, five, 3 * HOUR))},
    {'appId': claude, 'name': 'Claude · Work', 'color': 4, 'master': True,
     'usage': usage('max', 'alex@example.com', window(42, week, 4 * DAY), window(36, five, 1 * HOUR + 40 * 60), window(17, week, 4 * DAY, 'Fable'))},
    {'appId': codex, 'name': 'Open source', 'color': 2, 'share': True,
     'usage': usage('plus', 'oss@example.net', window(91, week, 1 * DAY + 20 * HOUR), window(60, five, 4 * HOUR))},
    {'appId': codex, 'name': 'Client · Acme', 'color': 3, 'share': True,
     'usage': usage('pro', 'alex@acme.example', window(17, week, 6 * DAY), window(8, five, 5 * HOUR))},
    {'appId': claude, 'name': 'Claude · Personal', 'color': 5,
     'usage': usage('pro', 'alex@example.org', window(70, week, 2 * DAY + 6 * HOUR), window(15, five, 2 * HOUR + 30 * 60))},
]
for p in profiles:
    p['id'] = uid()
state = {'schema': 2, 'apps': apps, 'profiles': profiles, 'projects': [], 'selected': codex, 'profileOrder': 1}
with open(os.path.join(root, 'state.plist'), 'wb') as f:
    plistlib.dump(state, f)
os.chmod(os.path.join(root, 'state.plist'), 0o600)
print(f'{len(profiles)} demo profiles in {root}')
