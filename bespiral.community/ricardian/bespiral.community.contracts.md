<h1 class="contract">create</h1>
---
spec-version: 0.0.1
title: Create new community
summary: Creates a new community on BeSpiral. It requires you to send: `cmm_asset`, `creator`, `logo`, `name`, `description`, `inviter_reward` and `invited_reward`. A BeSpiral community is closelly tied to a BeSpiral Token. A community allows a group of people with common goals and objectives to connect and allow the creation of incentives to reach those objectives. It also help to buy and sell products
icon:

<h1 class="contract">update</h1>
---
spec-version: 0.0.1
title: Update some information about a community
summary: Update information on a existing community on BeSpiral. It requires you to send: `cmm_asset`, `logo`, `name`, `description`, `inviter_reward` and `invited_reward`. All information will be saved, with the exception of the asset that cannot be changed
icon:

<h1 class="contract">netlink</h1>
---
spec-version: 0.0.1
title: Invites a new account to a given community
summary: Add a user to the BeSpiral community network. It requires you to send: `cmm_asset`, `inviter` and `new_user`. We'll save who invited the new account and on which community
icon:

<h1 class="contract">newobjective</h1>
---
spec-version: 0.0.1
title:
summary:
icon:

<h1 class="contract">newaction</h1>
---
spec-version: 0.0.1
title:
summary:
icon:

<h1 class="contract">claimaction</h1>
---
spec-version: 0.0.1
title:
summary:
icon:

<h1 class="contract">verifyclaim</h1>
---
spec-version: 0.0.1
title:
summary:
icon:

<h1 class="contract">verifyaction</h1>
---
spec-version: 0.0.1
title:
summary:
icon:

<h1 class="contract">createsale</h1>
---
spec-version: 0.0.1
title: Create a sale on a given community
summary: Enable a single user to create a new sale (either buy or sell) on a given community. It requires you to send: `from`, `title`, `description`, `quantity`, `image`, `is_buy` and `units`. All information sent is going to be saved. Note that `quantity` is related to price and `units` to number of items available.
icon:

<h1 class="contract">updatesale</h1>
---
spec-version: 0.0.1
title: Update a sale
summary: Enable the sale creator to update some details of a single sale. It requires you to send: `sale_id`, `title`, `description`, `quantity`, `image` and `units`. Except by `sale_id`, all information sent is going to be updated. Note that `quantity` is related to price and `units` to number of items available.
icon:

<h1 class="contract">deletesale</h1>
---
spec-version: 0.0.1
title: Delete a sale
summary: Enable the sale creator to remove a single sale. It requires you to send: `sale_id`. No information is going to be saved.
icon:

<h1 class="contract">removels</h1>
---
spec-version: 0.0.1
title: Delete a last sale
summary: Remove temporary information created during a sale creation. It requires you to send: `ls_id`. No information is going to be saved.
icon:

<h1 class="contract">reactsale</h1>
---
spec-version: 0.0.1
title: React to a sale
summary: Enable any user in the same community (except by creator) to react to a sale. It requires you to send: `sale_id`, `from` and `type`. No information is going to be saved.
icon:

<h1 class="contract">transfersale</h1>
---
spec-version: 0.0.1
title: Process a sale transfer
summary: Enable different users to exchange value for a given sale. It requires you to send: `sale_id`, `from`, `to`, `quantity` and `units`. No information is going to be saved, only used to update previous sale information. Note that `from` is the one interested in the sale, `to` the sale creator, `quantity` is related to price and `units` to number of items available.
icon:

<h1 class="contract">updobjective</h1>
---
spec-version: 0.0.1
title: updobjective placeholder
summary: placeholder
icon:

<h1 class="contract">upsertaction</h1>
---
spec-version: 0.0.1
title: upsertaction placeholder
summary: placeholder
icon:

<h1 class="contract">setindices</h1>
---
spec-version: 0.0.1
title: setindices placeholder
summary: placeholder
icon:

<h1 class="contract">deleteobj</h1>
---
spec-version: 0.0.1
title: deleteobj placeholder
summary: placeholder
icon:

<h1 class="contract">deleteact</h1>
---
spec-version: 0.0.1
title: deleteact placeholder
summary: placeholder
icon:

<h1 class="contract">migrate</h1>
---
spec-version: 0.0.1
title: migrate placeholder
summary: placeholder
icon:

<h1 class="contract">clean</h1>
---
spec-version: 0.0.1
title: clean placeholder
summary: placeholder
icon:

<h1 class="contract">migrateafter</h1>
---
spec-version: 0.0.1
title: migrateafter placeholder
summary: placeholder
icon: