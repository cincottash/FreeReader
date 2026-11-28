# Biblioglot privacy policy

*Last updated: November 28, 2026. The full policy, including Google Drive Sync, is on Biblioglot's website:
[cincottash.github.io/Biblioglot/privacy.html](https://cincottash.github.io/Biblioglot/privacy.html).*

Biblioglot is an e-book reader for Android, Windows and Linux, made by cincottash. This page says what happens to
your information when you use it. In short: **your books and everything you do with them stay on your device.
Biblioglot has no accounts, no ads, no analytics and no tracking, and its developer receives none of your data.**

## What stays on your device

- The books you add (Biblioglot keeps its own copy of each), your reading positions, highlights, notes, bookmarks,
  your Word List and flashcard progress, and your settings.
- These are kept in Biblioglot's private storage on your device. Android's own backup (to your Google account) may
  include them if you have device backup turned on; Biblioglot itself never uploads them anywhere.

## Google Drive Sync (optional)

Sync is off until you choose Sign In With Google in Settings. Then Biblioglot uses only its own hidden app data
folder in your Google Drive (scope `drive.appdata`; it cannot see any other Drive file) to keep your books, reading
positions, highlights, notes, bookmarks, Word List, flashcard progress, statistics and settings the same on your
devices. The data goes directly between your devices and your Drive; there is no Biblioglot server, and API keys are
never synced. Sign Out stops it; Google Drive > Settings > Manage Apps > Biblioglot > Delete Hidden App Data removes
it. Biblioglot's use and transfer to any other app of information received from Google APIs will adhere to the
[Google API Services User Data Policy](https://developers.google.com/terms/api-services-user-data-policy), including
the Limited Use requirements.

## What is sent over the internet, and only when you ask for it

- **Translations by Google Gemini (optional).** Gemini is used only after you paste your own Gemini API key into
  Settings. Then, when you look up a word or passage, Biblioglot sends the selected text and the sentence around it
  to Google's Gemini service, which returns the translation. Nothing else from your books is sent. Google handles
  that text under its own terms ([Gemini API terms](https://ai.google.dev/gemini-api/terms) and
  [Google's privacy policy](https://policies.google.com/privacy)); on Gemini's free tier Google may use it to improve
  its products. Your key is stored encrypted, with an encryption key that stays in Android's Keystore (on a computer:
  in the system's key store), and is sent only to Google. Without a key, no text leaves your device.
- **Dictionary downloads (optional).** When you tap Download in Settings, Biblioglot downloads the dictionary file
  from Biblioglot's page on GitHub. GitHub sees the request like any web download (your IP address); nothing else is
  sent.
- **Links you tap.** "Get a Free Key", "Full Definition" (Wiktionary) and "Report" open your web browser. "Report"
  opens a pre-filled issue on Biblioglot's GitHub page with the selected text and the answer being reported; nothing
  is sent unless you choose to submit it there, and what you submit is public on GitHub.

## On the desktop apps

The Windows and Linux apps work the same way. They can also use LM Studio, a program on your own computer: text sent
to it stays on your computer.

## Permissions

- **Internet and network state:** for Gemini lookups and dictionary downloads, only when you use them.
- **Files:** books are chosen with Android's own file picker; Biblioglot reads only the files you pick.

## Children

Biblioglot is a general-audience reading app and isn't directed at children. It collects no personal information
from anyone.

## Changes and contact

If this policy changes, the new version will be on this page with a new date. Questions or concerns: open an issue
at [github.com/cincottash/Biblioglot/issues](https://github.com/cincottash/Biblioglot/issues).
