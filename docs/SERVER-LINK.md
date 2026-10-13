# Tally server link (protocol v1)

Tally can bring in purchases from a server **you run yourself**, for example a phone-friendly web
page where you scan a barcode at the store and type the price. This file is the contract between
Tally and that server. Anyone can write one, in any language: it only needs the two requests below.

## Privacy rules (these don't bend)

- The link is **off** until the user turns it on (Settings → Server link).
- Tally contacts the server **only** when the user asks: `G` on Prices → Bought, "Get new
  purchases now" in Settings, or `tally --get-purchases`. Never in the background.
- Tally **only downloads**. A request carries two things: the pairing code (in an
  `Authorization` header) and the `since` bookmark the server handed out last time. Nothing from
  the budget (income, expenses, loans, ZIP, wants) is ever sent.
- Plain `http://` is accepted only for home-network addresses (`localhost`, `10.x`, `192.168.x`,
  `172.16-31.x`, `169.254.x`, Tailscale `100.64-127.x`, `*.local`, `*.lan`, `*.home.arpa`,
  `*.internal`, bare names like `myserver`). Anything else must be `https://`.
- Tally doesn't follow redirects, and the pairing code is passed to curl in a private temp file,
  not on the command line.

## Requests

Both are `GET` with `Authorization: Bearer <pairing code>`. The pairing code is 1–128 characters
from `A-Z a-z 0-9 - _ .`. Answer `401` for a wrong code.

### `GET /api/tally/v1/hello`

Connection check.

```json
{ "tally_link": 1, "name": "Kitchen" }
```

### `GET /api/tally/v1/purchases?since=<cursor>`

Everything added after `cursor`. The first request has no `since`. Cursors are opaque to Tally
(1–64 characters from `A-Z a-z 0-9 - _ .`); an increasing row id works fine.

```json
{
  "tally_link": 1,
  "cursor": "57",
  "more": false,
  "purchases": [
    { "id": "56", "date": "2026-10-12", "name": "Ground beef 1 lb", "amount": 5.49,
      "category": "Groceries", "store": "Aldi", "barcode": "0123456789012" },
    { "id": "57", "date": "2026-10-12", "name": "Milk, gallon", "amount": 3.19,
      "category": "Groceries", "store": "Aldi" }
  ],
  "food": { "month": "2026-10", "wasted": 12.40, "saved_by_freezing": 8.00 }
}
```

| Field | |
|---|---|
| `tally_link` | Protocol version. Tally refuses replies with a version newer than it knows. |
| `cursor` | The bookmark to send next time. |
| `more` | `true` if there's another page; Tally asks again right away (at most 50 pages). |
| `purchases[].id` | Unique and stable. Tally skips ids it already has, so resending is harmless. |
| `purchases[].date` | `YYYY-MM-DD`. Missing or malformed → Tally uses today. |
| `purchases[].amount` | Number (or numeric string). Negative = a return or refund. |
| `purchases[].category` | Matched to the user's Tally categories by name (case-insensitive). Unknown → "Other", missing → "Groceries". |
| `purchases[].store` | Optional. |
| `purchases[].barcode` | Optional; Tally ignores it today. |
| `food` | Optional. The server's food totals for one month: money lost to expired food, and money saved by freezing before the date. Shown as a Home checkup for the current month. |

Deleting a purchase in Tally doesn't touch the server, and because the cursor has moved past it,
Tally won't bring it back.

## Offline: the same data as a file

`Settings → Server link → Import purchases from a file` and `tally --import-purchases FILE` read
the exact same JSON as the `purchases` reply. The server should offer it as a download so purchases
can move on a USB stick with no network at all.
