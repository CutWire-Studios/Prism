#pragma once

/// The two fixed physical mixer slots. A take swaps their contents; the slots themselves never change role.
enum class Deck : int { Live = 0, Staging = 1 };

constexpr Deck otherDeck(Deck d) { return d == Deck::Live ? Deck::Staging : Deck::Live; }

constexpr bool isLive(Deck d) { return d == Deck::Live; }
