# Thunderbird 158 (Mozilla's mail client, 64-bit), installed the way a
# user gets it: the App Store's "Install" button unpacks Mozilla's full
# installer (the download its catalog lists, put in C:\Downloads) with
# 7-Zip into C:\Programs\Mozilla Thunderbird.  It then opens
# C:\Apps\data\hello.eml (a plain-text message the corpus makes) in its
# own message window: sender, subject, date and the text; the screenshot
# must match tests/reference/thunderbird.png.  A policies.json in
# Thunderbird's distribution folder (Mozilla's documented way to configure
# a deployment) turns off update checks, telemetry, the default mail
# client question, the start page and the account setup it offers when no
# account exists.  Windowed; like Firefox it is several processes of one
# executable.
import json, os

DOC = 'Thunderbird (opens an e-mail message)'
CORE = r'C:\Programs\Mozilla Thunderbird\core'


def settings(app, files, programs):
    """distribution\\policies.json beside thunderbird.exe"""
    d = os.path.join(programs, 'Mozilla Thunderbird', 'core', 'distribution')
    os.makedirs(d, exist_ok=True)
    policies = {'policies': {
        'DisableAppUpdate': True, 'DisableTelemetry': True, 'DisableDefaultClientAgent': True,
        'Preferences': {
            'mail.provider.suppress_dialog_on_startup': {'Value': True, 'Status': 'locked'},
            'mail.shell.checkDefaultClient': {'Value': False, 'Status': 'locked'},
            'mail.rights.override': {'Value': True, 'Status': 'locked'},
            'mailnews.start_page.enabled': {'Value': False, 'Status': 'locked'},
            'datareporting.policy.dataSubmissionPolicyBypassNotification': {'Value': True, 'Status': 'locked'}}}}
    with open(os.path.join(d, 'policies.json'), 'w') as f:
        json.dump(policies, f, indent=2)


APP = App('Thunderbird', '158.0',
          'https://archive.mozilla.org/pub/thunderbird/releases/158.0/win64/en-US/Thunderbird%20Setup%20158.0.exe',
          'Thunderbird', [Test('install from the App Store', 'store install Thunderbird', store='Thunderbird',
                               timeout=1500),
                          Test('open a message', rf'start "{CORE}\thunderbird.exe" {A}\data\hello.eml',
                               timeout=360)],
          unpack=settings, gui=True, store='Thunderbird', processes=True, closes=2)
