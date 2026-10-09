#include "MailModel.hpp"
#include <windows.h>
#include <cstring>

// Fictional people, companies, and `.example` domains (RFC 2606). Nothing here is sent anywhere.
namespace mail {
namespace {
using namespace std::chrono_literals;

struct SeedAttachment {
    const char* name;
    std::uint64_t bytes;
};

// Constant data: UTF-8 literals and plain arrays keep the table in read-only data with no
// startup initializer, at half the size of wide strings. sample() widens it on demand.
struct Seed {
    Folder folder;
    const char* sender;
    const char* address;
    const char* recipients;
    const char* subject;
    const char* body;
    std::chrono::minutes age;
    bool unread{};
    bool starred{};
    SeedAttachment attachments[2]{};
};

constexpr auto me = "Jordan Reyes <jordan@lumen.example>";
constexpr auto team = "studio@lumen.example";

constexpr Seed seeds[]{
    {Folder::inbox, "Priya Natarajan", "priya@lumen.example", me, "Sprint 42 review notes",
     R"(Hi Jordan,

Quick recap from this morning's review so nobody has to dig through the recording.

Shipped: the sparse atlas demo, device-loss recovery for texture streams, and the accessibility pass on buttons. The capture work landed too, although we agreed the picker shutdown path still wants a second look before release.

Carried over: text input, the composition-based list virtualization, and the installer packaging. Tomasz volunteered for the first two if you can take packaging.

Could you add your numbers to the velocity sheet before Thursday? Thanks for driving the demo, it landed well with the Harborlight team.

Priya)", 25min, false, true},
    {Folder::inbox, "Lumen CI", "builds@ci.lumen.example", team, "Nightly build #4121 passed",
     R"(Nightly build #4121 completed successfully.

Branch: main
Commit: e724b4a  Add sparse virtual drawing surfaces and a scrollable atlas demo
Duration: 14 min 32 s

Tests: 41 passed, 0 failed, 6 skipped (composition textures unsupported on the agent).

Artifacts: composia-demo.exe, composia-media-demo.exe, composia-virtual-demo.exe, test-results-all.xml

This message was generated automatically. Reply to the build channel if the result looks wrong.)", 68min, true},
    {Folder::inbox, "Tomasz Wierzbicki", "tomasz@lumen.example", me, "Re: Composition layer ordering on multi-monitor",
     R"(Jordan,

I reproduced the ordering glitch on the 144 DPI monitor. It only happens when the window straddles two displays during a DPI change: the child HWND targets resize before the parent root scale updates, so for one frame the buttons render at the old scale.

Attached is an ETW trace from the repro. The fix is probably to defer child set_bounds until after the root resize in the same render call, which is what the media demo already does by accident.

I'll write it up properly tomorrow, but wanted you to have the trace in case you look at it tonight.

Tomasz)", 2h + 12min, true, false, {{"trace-dpi-144.etl", 3355443}}},
    {Folder::inbox, "Hana Okafor", "hana@lumen.example", me, "Lunch on Thursday?",
     R"(Hey!

The new place on Quay Street finally opened and apparently the flatbreads are excellent. Thursday at 12:30?

Theo mentioned he'd like to come too if that's alright. It'd be a nice way for him to meet a few more people outside of standups.

Hana)", 3h + 5min, true},
    {Folder::inbox, "Orbit Calendar", "no-reply@orbit.example", me, "Reminder: Design sync at 15:00",
     R"(Design sync
Today, 15:00 to 15:45
Room: Lighthouse (2nd floor) / video link in the invite

Agenda
1. Harborlight reading app: updated brief walkthrough (Mira joining)
2. Kiosk typography shortlist
3. Open questions on the mail client prototype

You are receiving this because you accepted the invitation.)", 4h + 20min},
    {Folder::inbox, "Mira Castellanos", "mira@harborlightbooks.example", me, "Updated brief for the reading app",
     R"(Hi Jordan,

Thank you again for the walkthrough on Friday. I've attached version 3 of the brief with the changes we discussed:

- The library view should default to the "recently opened" shelf.
- Night mode needs a true black option for the e-ink devices.
- Annotations sync can be deferred to phase two.

The board was especially taken with the smooth page-turn demo. If you can keep that feel in the real app I think we have an easy approval.

Would next Tuesday work for a short review of the revised estimate?

Warm regards,
Mira Castellanos
Head of Digital, Harborlight Books)", 6h + 40min, true, true, {{"harborlight-brief-v3.pdf", 1887436}}},
    {Folder::inbox, "Kwame Mensah", "kwame@lumen.example", me, "Font licensing for the kiosk build",
     R"(Jordan,

Legal came back on the typeface question. The desktop license we have covers the kiosk builds as long as the fonts are embedded and not redistributable, which they aren't. We do need to add the attribution line to the about screen.

I've put the license PDF in the shared drive under Legal/Fonts. Let me know if you want me to handle the attribution text.

Kwame)", 24h + 30min},
    {Folder::inbox, "Fieldnotes", "digest@fieldnotes.example", me, "Your week in Fieldnotes",
     R"(Here is what you captured this week.

12 notes, 3 sketches, 1 voice memo.

Most revisited: "Composition z-order ideas", "Mail client layout", "Questions for Mira".

Tip: pin a note to keep it at the top of your inbox view. You can change how often you receive this digest in Settings.)", 26h},
    {Folder::inbox, "Elena Vasquez", "elena@lumen.example", me, "Accessibility audit results",
     R"(Hi Jordan,

The audit of the composition demos is done. Summary attached, headlines below.

Good: buttons expose name, role, enabled state and Invoke through UI Automation; keyboard focus is visible; contrast on the dark theme passes AA everywhere except the muted preview text in the list (3.9:1).

Needs work: text fields need a Value pattern provider, list rows need to be exposed as items, and the status bar changes should raise a live-region notification.

None of these are blockers for the prototype, but they will be for the kiosk release. Happy to pair on the text field provider.

Elena)", 28h + 15min, true, false, {{"audit-summary.xlsx", 421888}}},
    {Folder::inbox, "Sam Oyelaran", "sam@lumen.example", team, "Photos from the offsite",
     R"(Finally sorted through the offsite photos. Two of the better ones attached, the full album is in the shared drive under Events/Offsite.

Also: whoever left a very nice jacket in the minibus, it's at reception.

Sam)", 2 * 24h + 3h, false, false, {{"offsite-group.jpg", 2411520}, {"offsite-boats.jpg", 1960345}}},
    {Folder::inbox, "Northwind Freight", "billing@northwind-freight.example", me, "Invoice NF-20931 for September",
     R"(Dear Jordan Reyes,

Please find attached invoice NF-20931 for the September kiosk hardware deliveries.

Amount due: 4,860.00
Due date: 31 October

Payment details are on the invoice. Reply to this message with any questions about the shipment.

Northwind Freight Accounts)", 2 * 24h + 7h, false, false, {{"NF-20931.pdf", 188416}}},
    {Folder::inbox, "Ingrid Solberg", "ingrid@lumen.example", me, "Can you review my PR on the virtual surface cache?",
     R"(Hi Jordan,

I opened the PR that bounds the tile cache by viewport instead of a fixed count. The interesting part is the trim: I keep one tile of overscan on each side so wheel scrolling doesn't repaint on every tick.

Two things I'm unsure about:
1. Whether the painter should see the tile rect in document pixels or local pixels. I went with document pixels.
2. The behaviour when the viewport is larger than the budget allows. Right now it throws before touching the retained region.

Reviews welcome whenever you have a moment, no rush before Thursday.

Ingrid)", 3 * 24h + 2h},
    {Folder::inbox, "Lumen CI", "builds@ci.lumen.example", team, "Nightly build #4118 failed: tests/VirtualSurfaceTests",
     R"(Nightly build #4118 failed.

Branch: main
Failing test: virtual-surface-warp
Output: Wrong pixels in retained virtual surface region (expected green at 20,20)

The hardware variant passed. Most likely the WARP device needs a Flush before CopySurface.

This message was generated automatically.)", 3 * 24h + 9h},
    {Folder::inbox, "Noor Haddad", "noor@lumen.example", team, "Welcome aboard, Theo!",
     R"(Everyone,

Please welcome Theo Marchetti, who joins the studio today as a junior engineer. Theo comes from the games side and has already found the coffee machine.

Theo will be pairing with Tomasz for the first few weeks on the composition framework. Say hi when you see him.

Noor)", 4 * 24h + 1h},
    {Folder::inbox, "Dev Weekly", "hello@devweekly.example", me, "Issue 318: GPU text rendering, sparse surfaces, and more",
     R"(Dev Weekly, issue 318

In this issue:
- Why grayscale antialiasing wins on composited surfaces
- Sparse drawing surfaces: a practical introduction
- Keeping child HWNDs in sync with per-monitor DPI changes
- Reader question: when should a desktop app own its own message loop?

Plus the usual links, jobs and a reader-submitted crossword.

You subscribed with this address. Unsubscribe at any time from the footer.)", 4 * 24h + 6h},
    {Folder::inbox, "Harbor Coffee Roasters", "news@harborcoffee.example", me, "October roast: Yirgacheffe is back",
     R"(The washed Yirgacheffe is back for the autumn and it tastes like apricot jam.

Studio subscribers get a free bag with any order this month. Use the code at checkout or just mention it at the counter.

See you at the roastery,
Harbor Coffee)", 5 * 24h + 4h},
    {Folder::inbox, "Priya Natarajan", "priya@lumen.example", team, "Holiday cover for the week of the 20th",
     R"(Hi all,

I'm away the week of the 20th. Tomasz is covering sprint planning and Jordan is the point of contact for Harborlight.

If anything urgent comes up, Noor knows how to reach me.

Priya)", 5 * 24h + 9h},
    {Folder::inbox, "Tomasz Wierzbicki", "tomasz@lumen.example", me, "Device-loss recovery write-up",
     R"(Jordan,

As promised, the write-up on how recovery works end to end. Short version:

The graphics device registers a removal event with D3D11 and the application waits on it alongside the message queue. When it fires we recreate the D3D and D2D devices, hand the new device to the composition graphics device with SetRenderingDevice, and invalidate every window. Surfaces and visuals survive; only consumer caches need rebuilding.

The retry in Application::render covers the race where a draw fails before the event is observed. Persistent failures propagate so a broken driver doesn't loop forever.

I'd like to turn this into a page in the README eventually. Thoughts welcome.

Tomasz)", 6 * 24h + 5h, false, true},
    {Folder::inbox, "Meridian Credit Union", "alerts@meridian-cu.example", me, "Your October statement is ready",
     R"(Your statement for the account ending 4471 is now available.

Sign in to view or download it. For your security this message contains no links; open the app or type the address yourself.

Meridian Credit Union)", 8 * 24h + 2h},
    {Folder::inbox, "Hana Okafor", "hana@lumen.example", team, "Draft agenda for the Q4 planning day",
     R"(Hi everyone,

A rough agenda for the planning day on the 29th. Shout if anything is missing.

09:30 Coffee and a look back at Q3
10:00 Product: Harborlight, kiosk, and the mail prototype
12:00 Lunch
13:00 Engineering: framework roadmap and the text input work
15:00 Hiring and studio operations
16:00 Wrap up

Hana)", 9 * 24h + 3h},
    {Folder::inbox, "Rosa Lindqvist", "rosa@composeconf.example", me, "Speaking at Compose Conf 2027?",
     R"(Hello Jordan,

I run the programme for Compose Conf, a small conference about native UI and rendering. Your atlas demo made the rounds in our community and I'd love to invite you to speak next spring.

Sessions are 30 minutes plus questions. Travel and accommodation are covered. No pressure to decide now; the call for talks closes in December.

Best,
Rosa)", 11 * 24h + 6h},
    {Folder::inbox, "Theo Marchetti", "theo@lumen.example", me, "Thanks for the warm welcome",
     R"(Hi Jordan,

Thanks for taking the time yesterday. The walkthrough of the window and drawing scopes made the codebase click for me much faster than reading alone.

I've started on the text field spike Tomasz suggested. First question: should the caret be a composition visual or drawn into the surface? I'm leaning towards a visual so blinking doesn't redraw text.

Theo)", 12 * 24h + 1h},
    {Folder::inbox, "Lumen CI", "builds@ci.lumen.example", team, "Weekly dependency report",
     R"(Dependency report for the week.

Updates available: cppwinrt (patch), wil (patch).
Vulnerabilities: none reported for pinned versions.

Open the vcpkg baseline PR to apply the patch updates.

This message was generated automatically.)", 14 * 24h + 4h},
    {Folder::inbox, "Yusuf Demir", "yusuf@lumen.example", team, "Studio Wi-Fi password rotation",
     R"(The studio Wi-Fi password rotates on Monday morning. The new one will be on the whiteboard in the kitchen as usual and in the password manager under Studio/Network.

Guest network stays the same.

Yusuf)", 16 * 24h + 8h},

    {Folder::sent, "Jordan Reyes", "jordan@lumen.example", "Priya Natarajan <priya@lumen.example>", "Re: Sprint 42 planning",
     R"(Hi Priya,

Works for me. I'll take packaging and the README updates; happy for Tomasz to run with the list virtualization.

One ask: can we keep Thursday afternoon clear for the Harborlight estimate? Mira hinted the board wants a number before their next meeting.

Jordan)", 24h + 2h},
    {Folder::sent, "Jordan Reyes", "jordan@lumen.example", "Mira Castellanos <mira@harborlightbooks.example>", "Harborlight kickoff recap",
     R"(Hi Mira,

Thanks for a great kickoff. A quick recap of what we agreed:

- Phase one covers the library, reader, and night mode.
- We'll prototype the page-turn with real composition animations rather than video so you can feel the timing.
- You'll send the revised brief by end of week.

Looking forward to it.

Jordan)", 3 * 24h + 5h},
    {Folder::sent, "Jordan Reyes", "jordan@lumen.example", "Ingrid Solberg <ingrid@lumen.example>", "Re: PR review",
     R"(Ingrid,

Reviewed. Document pixels for the painter is the right call; it keeps the tile math honest at large offsets. I left two small comments on the trim rectangle and one on the error path.

Nice work on the overscan, scrolling feels great.

Jordan)", 3 * 24h + 1h},
    {Folder::sent, "Jordan Reyes", "jordan@lumen.example", "Noor Haddad <noor@lumen.example>", "Offsite expense report",
     R"(Hi Noor,

Expense report for the offsite attached: minibus fuel, the boat hire, and dinner on the second night.

Jordan)", 5 * 24h + 6h, false, false, {{"offsite-expenses.pdf", 96256}}},
    {Folder::sent, "Jordan Reyes", "jordan@lumen.example", team, "Design sync moved to Thursday",
     R"(All,

Design sync moves to Thursday at 15:00 this week so Mira can join. Same room, same link.

Jordan)", 7 * 24h + 2h},
    {Folder::sent, "Jordan Reyes", "jordan@lumen.example", "Kwame Mensah <kwame@lumen.example>", "Kiosk font shortlist",
     R"(Kwame,

Shortlist for the kiosk typeface, in order of preference:

1. The humanist sans we used in the atlas demo.
2. The condensed grotesque, if the licensing allows embedding.
3. System UI font as the fallback.

Can you check the licensing on the first two?

Jordan)", 10 * 24h + 4h},

    {Folder::drafts, "Jordan Reyes", "jordan@lumen.example", "Mira Castellanos <mira@harborlightbooks.example>", "Re: Updated brief for the reading app",
     R"(Hi Mira,

Thank you for the revised brief. Tuesday works for the estimate review; I'll send an invite for 11:00.

On the true-black night mode: )", 20min},
    {Folder::drafts, "Jordan Reyes", "jordan@lumen.example", "", "Notes for the conference talk",
     R"(Working title: "A canvas, a visual tree, and nothing else"

Outline
- Why we skipped XAML for the studio framework
- Drawing scopes and why EndDraw must always balance
- Sparse surfaces for documents and maps
- Live capture as just another layer
- What we got wrong: text input came last)", 2 * 24h + 5h},
    {Folder::drafts, "Jordan Reyes", "jordan@lumen.example", "Priya Natarajan <priya@lumen.example>", "Q4 goals (rough)",
     R"(Priya,

Rough Q4 goals for the framework track, for discussion on the planning day:

1. Ship the kiosk build on the composition framework.
2. Text input and list virtualization in the public API.
3. )", 6 * 24h + 3h},

    {Folder::archive, "Noor Haddad", "noor@lumen.example", me, "Welcome to Lumen Studio",
     R"(Hi Jordan,

Welcome! A few practical things for your first week:

- Your laptop and badge are at reception.
- The studio handbook is in the shared drive; the section on expenses is the one people ask about most.
- Standup is at 09:45 in Lighthouse.

Ask me anything, any time.

Noor)", 40 * 24h + 2h},
    {Folder::archive, "Lumen IT", "it@lumen.example", team, "Laptop refresh scheduled",
     R"(The laptop refresh for the engineering team is scheduled for the 12th. Back up anything that lives outside the shared drive before then.

New machines come with the SDK, clang-cl and CMake preinstalled.

Lumen IT)", 35 * 24h + 5h},
    {Folder::archive, "Mira Castellanos", "mira@harborlightbooks.example", me, "Harborlight contract signed",
     R"(Jordan,

Signed copy attached. The board approved the phase one budget this afternoon. Thank you for your patience through the procurement process.

Looking forward to the kickoff.

Mira)", 45 * 24h + 7h, false, true, {{"harborlight-msa-signed.pdf", 2621440}}},
    {Folder::archive, "Dev Weekly", "hello@devweekly.example", me, "Issue 312: Direct2D effects, DPI, and window lifetimes",
     R"(Dev Weekly, issue 312

In this issue:
- A tour of Direct2D effects for UI
- Per-monitor DPI without tears
- Who owns the HWND? Lifetimes in wrapper libraries

You subscribed with this address.)", 46 * 24h + 3h},
    {Folder::archive, "Sam Oyelaran", "sam@lumen.example", team, "Summer party photos",
     R"(Album is up in the shared drive under Events/Summer. Yes, the one with the inflatable flamingo is in there.

Sam)", 60 * 24h + 4h},

    {Folder::junk, "Prize Desk", "win@prize-desk.example", me, "You have been selected!!!",
     R"(Congratulations! Your address was selected in our monthly draw. Claim your reward within 24 hours by replying with your full details.

This offer expires soon. Act now.)", 24h + 6h, true},
    {Folder::junk, "Crypto Signals", "vip@crypto-signals.example", me, "Double your holdings this week",
     R"(Our members saw 200% gains last week. Join the VIP channel today and never miss a signal again.

Limited places available.)", 3 * 24h + 7h, true},
    {Folder::junk, "Warehouse Deals", "deals@warehouse-deals.example", me, "Final hours: 90% off everything",
     R"(Everything must go. Final hours of our biggest sale ever. Shop now before it's gone.)", 6 * 24h + 2h},

    {Folder::trash, "Orbit Calendar", "no-reply@orbit.example", me, "Reminder: Dentist at 09:30",
     R"(Dentist
Tomorrow, 09:30 to 10:00

You are receiving this because the reminder is set on your calendar.)", 2 * 24h + 9h},
    {Folder::trash, "Lumen CI", "builds@ci.lumen.example", team, "Nightly build #4110 passed",
     R"(Nightly build #4110 completed successfully.

Tests: 38 passed, 0 failed, 6 skipped.

This message was generated automatically.)", 9 * 24h + 8h},
    {Folder::trash, "Harbor Coffee Roasters", "news@harborcoffee.example", me, "September roast: a return to Huila",
     R"(The Huila is back for September. Chocolate, red apple, and a long finish.

See you at the roastery,
Harbor Coffee)", 30 * 24h + 5h},
};

std::wstring widen(const char* utf8) {
    const auto length = static_cast<int>(std::strlen(utf8));
    if (length == 0) { return {}; }
    std::wstring result(static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8, 0, utf8, length, nullptr, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, length, result.data(), static_cast<int>(result.size()));
    return result;
}
}

Mailbox Mailbox::sample(Clock::time_point now) {
    Mailbox mailbox;
    for (const auto& seed : seeds) {
        Message message;
        message.folder = seed.folder;
        message.sender = widen(seed.sender);
        message.address = widen(seed.address);
        message.recipients = widen(seed.recipients);
        message.subject = widen(seed.subject);
        message.body = widen(seed.body);
        for (const auto& attachment : seed.attachments) {
            if (attachment.name) { message.attachments.push_back({widen(attachment.name), attachment.bytes}); }
        }
        message.received = now - seed.age;
        message.unread = seed.unread;
        message.starred = seed.starred;
        mailbox.add(std::move(message));
    }
    return mailbox;
}

}
