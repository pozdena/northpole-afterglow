/* North Pole: the module's resources. Built with Apple's Rez:
 *   Rez src/module.r -o "build/North Pole" -t ADgm -c ADrk
 * Text is MacRoman; Pascal strings carry their length byte up front as $"..".
 * Slider resources 'sVal' 1000..1002 and checkbox 'xVal' 1003 arrive in
 * GMParamBlock.controlValues[0..3]; their resource names are the labels.
 */

/* Built three times: plain; with -d PREVIEW as "North Pole Preview", whose code pretends
   each scene is the next of the twelve days of Christmas; and with -d MOON as "North Pole
   Moon", for trying out the moon's phases (see src/module.c). */
#if defined(PREVIEW)
read 'ADgm' (0, "North Pole Preview", locked) "build/pv/code.bin";

data 'ADrk' (0, purgeable) {
    $"33" "North Pole Preview 1.5" $"0D" "The twelve days, one a scene"
};

data 'STR ' (128, "Credit Line") {
    $"2D" "North Pole Preview: the twelve days, any time"
};

data 'vers' (1) {
    $"0150 8000 0000"
    $"03" "1.5"
    $"16" "North Pole Preview 1.5"
};

data 'TEXT' (1000) {
    "NORTH POLE PREVIEW: the North Pole module, pretending it is one of "
    "the twelve days of Christmas, from December 14 (a partridge in a "
    "pear tree) to Christmas Day (twelve drummers drumming). Pick the day "
    "with the December slider; every scene shows that day's gift and that "
    "night's moon, and the date shows top left. With Show the gift right "
    "away on, the gift is out the moment a scene starts, and a parade keeps "
    "coming back. Your Mac's clock is not touched."
    $"0D0D"
#elif defined(MOON)
read 'ADgm' (0, "North Pole Moon", locked) "build/mn/code.bin";

data 'ADrk' (0, purgeable) {
    $"29" "North Pole Moon 1.5" $"0D" "Any phase of the moon"
};

data 'STR ' (128, "Credit Line") {
    $"26" "North Pole Moon: any phase of the moon"
};

data 'vers' (1) {
    $"0150 8000 0000"
    $"03" "1.5"
    $"13" "North Pole Moon 1.5"
};

data 'TEXT' (1000) {
    "NORTH POLE MOON: the North Pole module with the moon in whatever phase "
    "you pick on the Moon slider, from new through first quarter, full and "
    "last quarter back to new. Turn on Run through the phases and the moon "
    "goes through a whole month in about a minute. Top left is the moon's age "
    "in days, 0 (new) to 29. Set Snowfall to Never: clouds hide the moon."
    $"0D0D"
#else
read 'ADgm' (0, "North Pole", locked) "build/code.bin";

/* the name and credit After Dark shows */
data 'ADrk' (0, purgeable) {
    $"1E" "North Pole 1.5" $"0D" "Merry Christmas"
};

data 'STR ' (128, "Credit Line") {
    $"2A" "North Pole: a holiday module for Afterglow"
};

data 'vers' (1) {
    $"0150 8000 0000"
    $"03" "1.5"
    $"0E" "North Pole 1.5"
};

data 'TEXT' (1000) {
    "NORTH POLE: Santa, his elves, the reindeer and a few white bunnies "
    "potter about the workshop on a winter night, drawn the way a 1980 "
    "video game would have drawn them. Trees twinkle, the chimney smokes, "
    "and clouds hide the moon and stars while it snows. The moon shows "
    "tonight's phase."
    $"0D0D"
#endif
    "Snowfall: how often it snows."
    $"0D"
    "New scene every: when the time is up, Santa and Rudolph walk off and "
    "fly the sleigh across the sky, and then the scene is rebuilt somewhere "
    "new, so the trees and the workshop never sit in one place for long."
    $"0D"
    "Crowd: how many elves, reindeer and bunnies are out."
    $"00"
};

/* the heap After Dark should leave the module */
data 'sysz' (0, "North Pole heap requirement") {
    $"0001 4000"
};

#if defined(MOON)
data 'sVal' (1000, "Snowfall:") { $"0000" };          /* never: clouds hide the moon */
#else
data 'sVal' (1000, "Snowfall:") { $"0032" };
#endif
data 'sUnt' (1000, "Snowfall Units") {
    $"0005"
    $"0000" $"05" "Never"
    $"0019" $"06" "Rarely"
    $"0032" $"0C" "Now and then"
    $"004B" $"05" "Often"
    $"0064" $"06" "Always"
};

#if defined(PREVIEW)
data 'sVal' (1001, "New scene every:") { $"0000" };   /* a minute: a new day each minute */
#elif defined(MOON)
data 'sVal' (1001, "New scene every:") { $"0000" };
#else
data 'sVal' (1001, "New scene every:") { $"0016" };
#endif
data 'sUnt' (1001, "New scene Units") {
    $"000A"
    $"0000" $"05" "1 min"
    $"000B" $"05" "2 min"
    $"0016" $"05" "3 min"
    $"0021" $"05" "4 min"
    $"002C" $"05" "5 min"
    $"0037" $"05" "6 min"
    $"0042" $"05" "7 min"
    $"004D" $"05" "8 min"
    $"0058" $"05" "9 min"
    $"0063" $"06" "10 min"
};

#if defined(PREVIEW)
/* the preview's third slider is the date it pretends it is: December 14 .. 25 */
data 'sVal' (1002, "December:") { $"0000" };
data 'sUnt' (1002, "December Units") {
    $"000C"
    $"0000" $"02" "14"
    $"0009" $"02" "15"
    $"0012" $"02" "16"
    $"001B" $"02" "17"
    $"0024" $"02" "18"
    $"002D" $"02" "19"
    $"0037" $"02" "20"
    $"0040" $"02" "21"
    $"0049" $"02" "22"
    $"0052" $"02" "23"
    $"005B" $"02" "24"
    $"0064" $"02" "25"
};
#elif defined(MOON)
/* the moon test's third slider is the moon's phase */
data 'sVal' (1002, "Moon:") { $"0032" };
data 'sUnt' (1002, "Moon Units") {
    $"0005"
    $"0000" $"03" "New"
    $"0019" $"0D" "First quarter"
    $"0032" $"04" "Full"
    $"004B" $"0C" "Last quarter"
    $"0064" $"03" "New"
};
#else
data 'sVal' (1002, "Crowd:") { $"0032" };
data 'sUnt' (1002, "Crowd Units") {
    $"0003"
    $"0000" $"05" "Quiet"
    $"0032" $"04" "Busy"
    $"0064" $"06" "Packed"
};
#endif

#if defined(PREVIEW)
/* the preview's checkbox: the day's gift out from the start of every scene */
data 'xVal' (1003, "Show the gift right away") { $"0001" };
#elif defined(MOON)
data 'xVal' (1003, "Run through the phases") { $"0000" };
#else
data 'xVal' (1003, "Santa's sleigh") { $"0001" };
#endif
