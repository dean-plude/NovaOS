program: **Firefox 157** and **Floorp 12.19**
kind: program
rating: Platinum
works: Built into NovaOS as its default browser and also in the App Store; installs from the App Store, loads pages over HTTP and HTTPS, scrolls and takes typing in forms
problems: A publicly trusted HTTPS site not checked (the test network is offline)
checked: nightly corpus (Firefox, [`890-firefox.py`](../tests/appcorpus/890-firefox.py)), by hand in QEMU (Floorp)
