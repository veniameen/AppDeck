#!/usr/bin/env python3
"""Sandbox data for the dock self-test (APPDECK_DOCK_SELFTEST=1 with APPDECK_DATA_ROOT).

Two apps (ChatGPT/Codex and Claude) and N profiles stored in the app-interleaved order of state files
written before 0.9 (Claude profiles between Codex ones), so every run also exercises the one-time order
migration. Limits are switched off, nothing is shared and no profile is a master, so the sandbox never
asks for consent, never adopts a running window and never touches the user's data.

The target folder is DELETED first; the real AppDeck folder is refused.
Usage: make_dock_sandbox.py <folder> [profiles=9]"""
import os, plistlib, shutil, sys, uuid

if len(sys.argv) < 2:
    sys.exit(__doc__)
root = os.path.abspath(sys.argv[1])
if 'Application Support/AppDeck' in root or root in ('/', os.path.expanduser('~')):
    sys.exit('refusing to touch ' + root)
n = int(sys.argv[2]) if len(sys.argv) > 2 else 9
shutil.rmtree(root, ignore_errors=True)
for d in ('Profiles', 'Shared'):
    os.makedirs(os.path.join(root, d), mode=0o700, exist_ok=True)
os.chmod(root, 0o700)
codex, claude = str(uuid.uuid4()).upper(), str(uuid.uuid4()).upper()
apps = [
    {'id': codex, 'name': 'ChatGPT', 'path': '/Applications/ChatGPT.app', 'bundleId': 'com.openai.codex', 'adapter': 'codex', 'usageLimits': False},
    {'id': claude, 'name': 'Claude', 'path': '/Applications/Claude.app', 'bundleId': 'com.anthropic.claudefordesktop', 'adapter': 'claude', 'usageLimits': False},
]
names = ['Alpha', 'Beta', 'Gamma', 'Delta', 'Epsilon', 'Zeta', 'Eta', 'Theta', 'Iota', 'Kappa', 'Lambda', 'Mu']
profiles = []
for i in range(n):
    app = claude if i in (1, 4) else codex  # Claude profiles between Codex ones, as after adding them later
    prefix = 'Claude · ' if app == claude else ''
    profiles.append({'id': str(uuid.uuid4()).upper(), 'appId': app, 'name': prefix + names[i % len(names)] + ('' if i < len(names) else ' ' + str(i)),
                     'color': i % 6, 'share': app == codex})
state = {'schema': 2, 'apps': apps, 'profiles': profiles, 'projects': [], 'selected': codex}
with open(os.path.join(root, 'state.plist'), 'wb') as f:
    plistlib.dump(state, f)
os.chmod(os.path.join(root, 'state.plist'), 0o600)
print('\n'.join(p['name'] for p in profiles))
