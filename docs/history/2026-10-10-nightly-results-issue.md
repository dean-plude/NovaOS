## Nightly corpus results on the tracking issue

A scheduled nightly that runs the app corpus now posts its results to
issue #210: the jobs' outcomes, how many programs passed, each program's
version, result, checks and time, the smpstress log on four CPUs, and a link
to the run's logs and screenshots.  Runs whose gate skips a commit that was
already tested post nothing, so the issue only gains a comment when there
is something new to read.
