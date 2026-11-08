# Try xWeatherRouting 2.1.0 on Windows

Please try the new passage-planning screen and tell Paul what feels clear,
confusing or missing. **You do not need to compile anything for this first look.**

## Download and open

1. [Download the Windows trial ZIP (about 82 MiB)](https://github.com/pob220/xweather_routing_pi/releases/download/windows-ui-trial-2.1.0/xWeatherRouting-2.1.0-Windows-UI-Trial.zip).
2. Right-click the downloaded ZIP and choose **Extract All**. Use a new folder
   on your Desktop, then open that extracted folder.
3. Double-click **Start-Weather-Routing.cmd**. Allow a few seconds for the
   Weather Routing window to open in the included OpenCPN test copy.

The ZIP includes a matching 64-bit OpenCPN and the plugin, already enabled.
It keeps its settings and test files in the extracted folder. Your normal
OpenCPN, plugins, charts and routes are left alone. Use the supplied launcher
each time. To remove the trial, close its windows and delete that extracted folder.

## First calculation — no weather download needed

1. On **Plan passage**, choose **Passage actions → Try an example passage**.
2. Close the planning-help note and click **Calculate route**.
3. Look at the result, then try **Edit route**, **Boat and polar library** and
   **Passage settings**. Change something and calculate again.

The example selects the illustrative 40 ft monohull and bundled Climatology.
Climatology describes typical seasonal conditions, not today's forecast.
No separate Climatology or Polars plugin is needed for this exercise.

If you want to try one of your own OpenCPN routes, add test charts and a copy
of the route to this test copy first. Then try **Use OpenCPN route**, or its
right-click routing action. The trial starts without your personal charts/routes.

## What feedback would help?

- Is it obvious what to do in **Route, Boat and Weather** to get a calculation?
- Are the boat/weather choices and readiness messages understandable?
- What feels awkward, takes too many clicks, or is difficult to read?

Screenshots and the steps that confused you are useful. Please mention your
Windows display scaling if anything is clipped or too small. The aim of this
first look is feedback on using the interface; a build-system review is optional.

The trial contains OpenCPN 5.15.0 / wxWidgets 3.3.4 and xWeatherRouting 2.1.0.
It is separate from the stable 1.x catalogue version. Custom chart-aware hosts
can be tested separately. For source builds, see [Windows developer instructions](WINDOWS.md).

[The delivered ZIP passed native Windows launcher and route checks](https://github.com/pob220/xweather_routing_pi/actions/runs/42423542646), including preservation of a normal OpenCPN profile.
