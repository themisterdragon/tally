#include "learn.h"

const std::vector<Lesson> &lessons() {
    static const std::vector<Lesson> l = {
        {"Start here: what a budget is",
         "A budget is a plan for your money before you spend it. It isn't about saying no to everything. It's about "
         "deciding on purpose.\n\n"
         "Tally's version has three steps:\n"
         "1. Income: what actually reaches your bank account (take-home pay).\n"
         "2. Expenses: every bill and regular cost, whether it comes weekly, monthly, or once a year.\n"
         "3. The difference: money left over to save, pay down debt, or enjoy.\n\n"
         "Don't worry about being exact. A good guess today beats a perfect budget you never finish. You can change "
         "any number later and everything recalculates."},
        {"Gross pay vs. take-home pay",
         "Gross pay is the number in your job offer: what you earn before anything comes out.\n\n"
         "Take-home pay (net pay) is what lands in your bank account. The difference goes to taxes and deductions:\n"
         "- Federal income tax\n- Social Security (6.2%) and Medicare (1.45%), together called FICA\n"
         "- State and sometimes local income tax\n- Things you chose: 401(k), health insurance\n\n"
         "Always budget with take-home pay. Budgeting with gross pay is one of the most common first-budget "
         "mistakes, because that money was never yours to spend."},
        {"How tax brackets really work",
         "Moving into a higher bracket does NOT make all your income taxed at the higher rate. Only the dollars "
         "inside each bracket are taxed at that bracket's rate.\n\n"
         "Example (single, 2026): the first $12,400 of taxable income is taxed at 10%, the next dollars up to "
         "$50,400 at 12%, and so on.\n\n"
         "So a raise never lowers your take-home pay. Your marginal rate is the rate on your next dollar. Your "
         "effective rate is your total tax divided by your income, and it's always lower. Tally shows both on the "
         "Income tab."},
        {"Pre-tax deductions: 401(k) and health",
         "Some deductions come out before income tax is figured. That lowers your taxable income, so they cost "
         "less than they look.\n\n"
         "Putting $100 into a traditional 401(k) might only shrink your paycheck by $78-$88, because you skip the "
         "income tax on it for now (you pay it in retirement).\n\n"
         "Employer match: many jobs add money when you contribute, like 50 cents per dollar up to 6% of pay. Not "
         "taking the full match is turning down part of your pay."},
        {"Weekly, every 2 weeks, twice a month...",
         "Bills come on different schedules, so Tally turns everything into a monthly amount:\n"
         "- Weekly: x 52 / 12 (about 4.33 times a month, not 4!)\n"
         "- Every 2 weeks: x 26 / 12 (about 2.17 a month)\n"
         "- Twice a month: x 24 / 12 = 2 a month\n"
         "- Yearly: / 12\n\n"
         "Paid every 2 weeks? Two months a year have three paychecks. Many people budget on two paychecks a month "
         "and treat the third as a bonus for savings or debt."},
        {"Needs, wants, and 50/30/20",
         "Needs: what you must pay to live and work: housing, utilities, groceries, transportation, insurance, "
         "minimum debt payments.\n"
         "Wants: what makes life nicer but could be cut: eating out, streaming, hobbies, upgrades.\n\n"
         "The 50/30/20 guide splits take-home pay into about 50% needs, 30% wants, and 20% savings and extra debt "
         "payments.\n\n"
         "It's a starting point, not a rule. In a high-rent area needs might be 60%, and that's OK. Mark each "
         "expense as a need or a want and Tally shows your split on Home."},
        {"Emergency fund",
         "An emergency fund is money set aside for surprises: a car repair, a vet bill, a missed shift. It's what "
         "keeps a bad week from becoming credit card debt.\n\n"
         "Step 1: save $500-$1,000.\nStep 2: build to one month of needs.\nStep 3: grow to three to six months of "
         "needs.\n\n"
         "Keep it somewhere separate and boring, like a savings account, so it's there when you need it and not "
         "too easy to spend. Enter what you have under Income > Household > Savings now."},
        {"APR and how interest adds up",
         "APR (annual percentage rate) is the yearly cost of borrowing. Each month the lender charges about "
         "APR / 12 on what you still owe.\n\n"
         "Example: $5,000 at 24% APR costs about $100 in interest in the first month alone.\n\n"
         "Early payments are mostly interest; later payments are mostly principal (the amount you borrowed). "
         "That's why extra payments early on save so much: every extra dollar goes straight to principal and "
         "stops earning interest for the lender. Try it on the Loans tab with + and -."},
        {"Snowball vs. avalanche",
         "Two ways to pay off several debts. Both pay the minimum on everything and put all extra money on one "
         "debt at a time. When that one's gone, its payment rolls to the next.\n\n"
         "Avalanche: highest interest rate first. Saves the most money.\n"
         "Snowball: smallest balance first. Quick wins keep you motivated.\n\n"
         "The best plan is the one you'll stick with. The Loans tab compares both for your debts."},
        {"Inflation and your budget",
         "Inflation is prices rising over time. The government measures it with the Consumer Price Index (CPI), "
         "which tracks the cost of everyday things like groceries, rent, gas, and doctor visits.\n\n"
         "Different things rise at different speeds. Rent might jump 5% while clothing barely moves. Tally checks "
         "each spending category against the CPI for your region and estimates what your budget could cost a year "
         "from now.\n\n"
         "If your raises are smaller than inflation, your money buys less each year even though the number on your "
         "paycheck went up."},
        {"Real hourly pay and wants",
         "Your real hourly pay is your take-home pay divided by the hours you actually work. It's usually much "
         "lower than your wage, because taxes and deductions come out first.\n\n"
         "Thinking about price in hours of work makes it concrete. A $60 dinner might really cost 4 hours of your "
         "life.\n\n"
         "The Wants tab does this math for anything you're considering. It isn't there to say no. It helps you "
         "decide if something is worth it to you. Some things are!"},
        {"Sinking funds: yearly bills without surprises",
         "Some costs only come once or twice a year: car registration, insurance, holidays, school supplies. "
         "They feel like emergencies even though you knew they were coming.\n\n"
         "A sinking fund fixes that: divide the yearly cost by 12 and set that much aside every month. Tally does "
         "the division for you when you pick 'Yearly' or 'Twice a year'. The monthly column is what to save each "
         "month."},
        {"Your privacy in Tally",
         "Everything you type stays on this computer in one plain text file (see Settings for where). Tally has "
         "no account, no tracking, and no ads.\n\n"
         "Tally only uses the internet when you open Update and choose to download. Even then it sends the same "
         "fixed request every Tally user sends (all U.S. regions at once). Your ZIP code, income, and expenses are "
         "never part of it.\n\n"
         "Your ZIP is looked up in data built into Tally, so it works with no internet at all."},
    };
    return l;
}

const char *screen_help(int tab) {
    switch (tab) {
        case 0: return "HOME is your budget at a glance.\n\n"
                       "Take-home pay is what reaches your bank account each month. Spending is every expense and loan "
                       "payment, turned into a monthly amount. Left over is what's not assigned yet.\n\n"
                       "The colored bar shows how each month's pay is split. Each color is a spending category, and "
                       "the list underneath names them.\n\n"
                       "Checkups are friendly rules of thumb based on your own numbers:\n"
                       "  ok  = looking good\n  --  = worth a look\n  !!  = needs attention\n\n"
                       "They're guides, not grades.";
        case 1: return "INCOME is where Tally estimates your take-home pay.\n\n"
                       "Add each job or income source with A. For a job, enter your pay the way you know it (yearly "
                       "salary, hourly wage, or gross per paycheck) and how often you're paid.\n\n"
                       "Household (H) holds things that affect taxes for everyone: ZIP code (sets your state), filing "
                       "status, and kids/dependents. Some cities and counties charge a local income tax (an occupational "
                       "or earnings tax). If yours does, enter its percent there.\n\n"
                       "Pay that changes (overtime, tips, shifts)? Press M and type the take-home that really came in "
                       "this month. Tally compares it with the estimate on Home.\n\n"
                       "The paycheck bar shows where each paycheck goes. These are estimates for planning. Your real "
                       "withholding depends on your W-4 and other details.";
        case 2: return "EXPENSES lists every regular cost.\n\n"
                       "A = add your own, T = pick from a list of common bills (fastest way to start).\n"
                       "Enter = edit, D = delete, U = undo a delete.\n"
                       "S = change the sort, F = show one category.\n\n"
                       "Enter each amount however it comes (weekly, every 2 weeks, yearly...) and Tally turns it into a "
                       "monthly amount.\n\n"
                       "Weekly bills: $200 a week shows as $866.67 a month, not $800. That's on purpose: a year has 52 "
                       "weeks, not 48, so most months have 4 payments ($800) and four months a year have 5 ($1,000). "
                       "$866.67 is the average that keeps the year honest. Give the bill 'A day you pay it' and Tally "
                       "knows exactly which months get 5. Billed a flat $800 a month instead? Set How often to Monthly.\n\n"
                       "Need or want: needs are what you must pay; wants could be cut if you had to. This powers the "
                       "50/30/20 checkup on Home.\n\n"
                       "Bills that change (electric, water)? Press M when the bill comes and type what you really "
                       "paid this month. The plan stays the same and Tally shows the difference. In a bill's form, "
                       "'Other months' fixes earlier months, and after two or more months 'Use my average as the plan' "
                       "updates the plan for you.\n\n"
                       "Loans from the Loans tab are listed here too, marked '(Loans tab)', because they're part of "
                       "your monthly budget. Don't add them again as an expense; Tally warns you if it sees one twice.\n\n"
                       "Due dates: Enter on 'Next due date' opens a calendar. Pick the full date: for yearly or "
                       "twice-a-year bills the month matters.\n\n"
                       "C = Credit cards. List your cards by nickname only (like 'Visa'; never the card number). "
                       "Open a card and tick off the bills charged to it. When you pay the card, press M and type what "
                       "you paid: that one payment covers those bills, so nothing is counted twice. Tally shows how "
                       "much of the payment was the bills and how much was other spending on the card. You can also pick the "
                       "card on any bill with 'Charged to', or add a new card right there.\n\n"
                       "Other months: Left/Right (or [ and ]) step back and forward a month. The 'paid' column, M, and "
                       "the totals at the bottom follow the month shown, so you can fix last month's electric bill or "
                       "look ahead. N comes back to this month.\n\n"
                       "V = Calendar. The month as a calendar, with a colored dot on each day a bill is due (the dot is "
                       "the category's color) and a green + on days extra money came in. Arrows pick a day; Enter opens "
                       "it to type what you paid, or add a one-time cost or money that came in. Bills without a due date are listed under "
                       "the calendar: give them one to see them on it.\n\n"
                       "X = One-time money in and out, for the month shown. Out: a cost that happens once, like a car "
                       "repair, a vet visit, or a gift to buy (you can add one ahead of time to plan for it). In: birthday "
                       "or holiday money, a gift, a side hustle, selling something. Both count only in that month's "
                       "totals (money in, out, and left over) and never change your monthly plan. "
                       "Side-hustle money is taxable income; Tally shows how much you've made this year and when "
                       "self-employment tax kicks in ($400).\n\n"
                       "Esc goes back to Home from any tab.";
        case 3: return "LOANS shows how long your debts will take and what extra payments save.\n\n"
                       "Add a loan with A: the balance you owe now, the APR (interest rate), and either the monthly "
                       "payment or the months left.\n\n"
                       "Try it: select a loan and press + or - to change the extra monthly payment by $25. Watch the "
                       "payoff date and interest saved change. L sets a one-time extra payment.\n\n"
                       "The bottom compares snowball (smallest balance first) with avalanche (highest rate first) "
                       "for all your loans. E saves a month-by-month payment schedule.";
        case 4: return "WANTS answers: is this worth it?\n\n"
                       "Type the name and price of something you're thinking about buying. Tally shows:\n"
                       "- Hours of work it costs, at your real hourly pay (take-home / hours worked)\n"
                       "- How much of this month's leftover money it would use\n"
                       "- How long to save for it without debt\n\n"
                       "Your real hourly pay uses the hours per week set on each job in the Income tab. Bought it or "
                       "changed your mind? Delete it with D.\n\n"
                       "Trips and vacations: press A and choose 'A trip or vacation'. Fill in the miles (round trip), "
                       "your car's miles per gallon, nights and the cost per night, how many people, and meals out. "
                       "Tally adds up gas (at your region's average price unless you type one), wear on the car, lodging, "
                       "food, tickets, and fun. Set the month of the trip to see how much to set aside each month, and "
                       "add that to your budget with one press.";
        case 5: return "MY AREA compares your budget with where you live.\n\n"
                       "Tally looks up your ZIP code in Census data built into the program (nothing is sent anywhere). "
                       "The medians are the middle value: half of households are above and half below.\n\n"
                       "Median rent is gross rent: rent plus utilities. Average prices come from the Bureau of Labor "
                       "Statistics for your region of the country.";
        case 6: return "PRICES shows inflation for each kind of spending in your region.\n\n"
                       "Each row is a spending category. The percent is how much prices changed over the last 12 "
                       "months, and the little chart shows the last two years. A higher line means higher prices.\n\n"
                       "N switches between your region and the U.S. average.\n\n"
                       "B = Bought: groceries and other things you bought, typed in or brought in from your own server "
                       "(Settings > Server link, then G). It shows spending and food-waste trends next to your plan. "
                       "It's a look at trends only: purchases never change your expenses or totals, so a grocery "
                       "card's payment and your grocery purchases are never counted twice. Left/Right shows other months.\n\n"
                       "Prices are built in. To refresh them, use Update in Settings. Update only runs when you choose "
                       "it.";
        case 7: return "LEARN has short lessons on budgeting basics. Pick one with the arrow keys or the mouse. "
                       "Start with 'Start here' if you're new.";
        case 8: return "PRINT & EXPORT saves your budget to share, print, or keep.\n\n"
                       "- Printable page (PDF): summary, color charts, every expense, taxes, loans, prices.\n"
                       "- Spreadsheet (.xlsx): opens in Excel, LibreOffice, Google Sheets, or Numbers. It uses real "
                       "formulas: change an amount and the totals update.\n"
                       "- CSV: a simple table almost any program can read.\n"
                       "- Text: plain text that prints anywhere.\n\n"
                       "Files are saved to the folder shown and are never uploaded.";
        case 9: return "SETTINGS\n\n"
                       "Colors: use 'Basic' on old terminals or 'None' for no color.\n"
                       "Plain ASCII: draws charts with # - | instead of special characters.\n"
                       "Categories: rename, recolor, or add spending categories.\n\n"
                       "Update Tally: gets the newest version of Tally from GitHub and installs it, only when you "
                       "choose it. It checks the download's fingerprint and that it runs before swapping it in, and "
                       "your budget isn't touched. Installed for everyone (like /usr/local/bin)? It asks for your password.\n\n"
                       "Update tax tables & prices: downloads newer tax tables and prices, only when you choose it. It shows what it will "
                       "download first. No internet? Import a data pack from a file or USB stick instead.\n\n"
                       "Server link (optional, off by default): connects to a server you run yourself, like a phone page "
                       "for scanning barcodes. Tally only downloads purchases from it and never uploads your budget.\n\n"
                       "Your budget is one plain text file, and Tally keeps dated backups beside it.\n\n"
                       "Esc goes back to Home. Number keys 1-0 or Tab jump to any tab.";
    }
    return "";
}
