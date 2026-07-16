---
meco: 2
module: npc
sampler: diverse/1

types:
  Mood: [calm, tense]

inputs:
  playerName: text
  itemCount: number
  mood: Mood

imports:
  common: "./common.meco"

exports: [pickup, greeting, warning]
---

# pickup
- [3] &pickup-common <- player: $playerName, count: $itemCount
- [1] {mood is tense}
  &pickup-alert <-
    player: $playerName
    count: $itemCount

# local-intro
- @{common.name as hero} arrived. $hero looked tired.

# localized-arrival
- {common.name as hero}
  &arrival <- hero: $hero

# localized-encounter
- {common.name as hero}
  {common.name as companion}
  {common.place as destination}
  &encounter <- $hero, $companion, $destination

# title-suffix
- [3] ""
- [1] " the "@common.title

# multiline-example
- |
  First line.
  Second line.

# tense-arrival
- [1] {mood is tense}
  {common.name as hero}
  &arrival <- $hero

# tense-arrival-with-companion
- [1] {mood is tense}
  {common.name as hero}
  {common.name as companion}
  &arrival <- hero: $hero, $companion

<!--Basic composition and public rules.-->
# greeting
- [3] @salutation, @person!
- [1] @person, @observation.

# greetings <- name: text
- Hello, $name!
- Welcome back, $name!

# player-greeting
- @greetings <- name: $playerName

# warning
- Attention, @person: @observation.

# salutation
- Hello
- Good morning
- Welcome

# person
- traveller
- neighbour
- old friend

# observation
- the weather has changed
- the market is unusually quiet
- today feels promising

<!--A minimal subject-predicate grammar.-->
# sentence
- @subject @predicate.

# subject
- The pilot
- A maintenance drone

# predicate
- is waiting
- found the missing tool

<!--Ordinary unweighted alternatives.-->
# temperature
- cold
- mild
- uncomfortably warm

<!--References embedded in terminal text.-->
# report
- The @device is @condition.

# device
- air recycler
- navigation console

# condition
- offline
- making a strange noise

<!--Integer and decimal relative weights. An omitted weight is 1.-->
# weighted-mood
- [6] calm
- [3] tired
- [1] furious
- [0.5] cautiously optimistic

<!--Empty output, optional text, and an explicitly delimited adjacent reference.-->
# titled-greeting
- Welcome, @{name}@title-option.

# title-option
- [3] ""
- [1] " the "@title

# name
- Ada
- Tomas

# title
- Captain
- Doctor

<!--A delimited reference separates the rule name from a literal suffix.-->
# creature-count
- Several @{creature}s arrived.

# creature
- traveller
- maintenance drone

<!--Productive recursion with a strongly preferred terminating production.-->
# inventory
- [5] @item
- [1] @item, @inventory

# item
- a coil of wire
- a repair kit
- an empty canister

# calm-line
- Everything is under control.

<!--Escape a sigil when it should be emitted literally.-->
# contact
- Send a message to pilot\@example.invalid.

<!--A raw string does not interpret sigils or escape sequences.-->
# raw-contact
- r"Send a message to pilot@example.invalid."

<!--A raw block keeps every sigil as literal text and strips its final newline.-->
# raw-sigils
- |raw-
  @person, $playerName, and &pickup-alert are literal text.

<!--Split incompatible concepts into semantically coherent branches.-->
# incident
- @repair-incident
- @travel-incident

# repair-incident
- @technician @repair-action @broken-device.

# travel-incident
- @traveller @travel-action @destination.

# technician
- The mechanic
- A service drone

# repair-action
- inspected
- repaired

# broken-device
- the air recycler
- the navigation console

# traveller
- The courier
- A survey team

# travel-action
- departed for
- finally reached

# destination
- the northern outpost
- the orbital terminal
