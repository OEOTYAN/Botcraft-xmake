#include "botcraft/Game/AssetsManager.hpp"
#include "botcraft/Game/Inventory/InventoryManager.hpp"
#include "botcraft/Game/Inventory/Window.hpp"
#include "botcraft/Utilities/Logger.hpp"

using namespace ProtocolCraft;

namespace Botcraft
{
    InventoryManager::InventoryManager()
    {
        std::scoped_lock<std::shared_mutex> lock(inventory_manager_mutex);
        index_hotbar_selected = 0;
        cursor = Slot();
        inventories[Window::PLAYER_INVENTORY_INDEX] = std::make_shared<Window>(InventoryType::PlayerInventory);
#if PROTOCOL_VERSION > 451 /* > 1.13.2 */
        trading_container_id = -1;
#endif
    }


    void InventoryManager::SetSlot(const short window_id, const short index, const Slot& slot)
    {
        std::shared_ptr<Window> window = nullptr;

        {
            std::shared_lock<std::shared_mutex> lock(inventory_manager_mutex);
            auto it = inventories.find(window_id);
            if (it != inventories.end())
            {
                window = it->second;
            }
        }

        if (window == nullptr)
        {
            // In 1.17+ we don't wait for any server confirmation, so this can potentially happen very often.
#if PROTOCOL_VERSION < 755 /* < 1.17 */
            LOG_WARNING("Trying to add item in an unknown window with id : " << window_id);
#endif
        }
        else
        {
            window->SetSlot(index, slot);
        }
    }

    std::shared_ptr<Window> InventoryManager::GetWindow(const short window_id) const
    {
        std::shared_lock<std::shared_mutex> lock(inventory_manager_mutex);
        auto it = inventories.find(window_id);
        if (it == inventories.end())
        {
            return nullptr;
        }
        return it->second;
    }

    std::shared_ptr<Window> InventoryManager::GetPlayerInventory() const
    {
        return GetWindow(Window::PLAYER_INVENTORY_INDEX);
    }

    short InventoryManager::GetIndexHotbarSelected() const
    {
        std::shared_lock<std::shared_mutex> lock(inventory_manager_mutex);
        return index_hotbar_selected;
    }

    short InventoryManager::GetFirstOpenedWindowId() const
    {
        std::shared_lock<std::shared_mutex> lock(inventory_manager_mutex);
        for (const auto& [id, ptr] : inventories)
        {
            if (id != Window::PLAYER_INVENTORY_INDEX &&
                ptr->GetSlots().size() > 0)
            {
                return id;
            }
        }

        return -1;
    }

    Slot InventoryManager::GetHotbarSelected() const
    {
        std::shared_ptr<Window> inventory = GetPlayerInventory();

        if (inventory == nullptr)
        {
            return Slot();
        }

        return inventory->GetSlot(Window::INVENTORY_HOTBAR_START + index_hotbar_selected);
    }

    Slot InventoryManager::GetOffHand() const
    {
        std::shared_ptr<Window> inventory = GetPlayerInventory();

        if (inventory == nullptr)
        {
            return Slot();
        }

        return inventory->GetSlot(Window::INVENTORY_OFFHAND_INDEX);
    }

    void InventoryManager::EraseInventory(const short window_id)
    {
        std::scoped_lock<std::shared_mutex> lock(inventory_manager_mutex);
#if PROTOCOL_VERSION < 755 /* < 1.17 */
        pending_transactions.erase(window_id);
        transaction_states.erase(window_id);
#else
        // In versions > 1.17 we have to synchronize the player inventory
        // when a container is closed, as the server does not send info
        SynchronizeContainerPlayerInventory(window_id);
#endif
        // Some non-vanilla server sends a CloseContainer packet on death
        // Player inventory should not be removed from the map
        if (window_id == Window::PLAYER_INVENTORY_INDEX)
        {
            inventories[Window::PLAYER_INVENTORY_INDEX]->Init();
        }
        else
        {
            inventories.erase(window_id);
        }

#if PROTOCOL_VERSION > 451 /* > 1.13.2 */
        if (window_id == trading_container_id)
        {
            trading_container_id = -1;
            available_trades.clear();
        }
#endif
    }

#if PROTOCOL_VERSION < 755 /* < 1.17 */
    TransactionState InventoryManager::GetTransactionState(const short window_id, const int transaction_id) const
    {
        std::shared_lock<std::shared_mutex> lock(inventory_manager_mutex);

        auto it = transaction_states.find(window_id);
        if (it == transaction_states.end())
        {
            LOG_ERROR("Asking state of a transaction for a closed window");
            return TransactionState::Refused;
        }

        auto it2 = it->second.find(transaction_id);
        if (it2 == it->second.end())
        {
            LOG_ERROR("Asking state of an unknown transaction");
            return TransactionState::Refused;
        }

        return it2->second;
    }

    void InventoryManager::AddPendingTransaction(const InventoryTransaction& transaction)
    {
        std::scoped_lock<std::shared_mutex> lock(inventory_manager_mutex);

        pending_transactions[transaction.packet->GetContainerId()].insert(std::make_pair(transaction.packet->GetUid(), transaction));
        transaction_states[transaction.packet->GetContainerId()].insert(std::make_pair(transaction.packet->GetUid(), TransactionState::Waiting));

        // Clean oldest transaction to avoid infinite growing map
        for (auto it = transaction_states[transaction.packet->GetContainerId()].begin(); it != transaction_states[transaction.packet->GetContainerId()].end(); )
        {
            if (std::abs(it->first - transaction.packet->GetUid()) > 25)
            {
                it = transaction_states[transaction.packet->GetContainerId()].erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
#else
    void InventoryManager::SynchronizeContainerPlayerInventory(const short window_id)
    {
        // No lock as this is called from already scoped lock functions
        if (window_id == Window::PLAYER_INVENTORY_INDEX)
        {
            return;
        }

        auto it = inventories.find(window_id);
        if (it == inventories.end())
        {
            LOG_WARNING("Trying to synchronize inventory with a non existing container");
            return;
        }

        short player_inventory_first_slot = it->second->GetFirstPlayerInventorySlot();
        std::shared_ptr<Window> player_inventory = inventories[Window::PLAYER_INVENTORY_INDEX];

        for (int i = 0; i < Window::INVENTORY_HOTBAR_START; ++i)
        {
            player_inventory->SetSlot(Window::INVENTORY_STORAGE_START + i, it->second->GetSlot(player_inventory_first_slot + i));
        }
    }
#endif

#if PROTOCOL_VERSION > 755 /* > 1.17 */
    void InventoryManager::SetStateId(const short window_id, const int state_id)
    {
        std::shared_lock<std::shared_mutex> lock(inventory_manager_mutex);
        auto it = inventories.find(window_id);

        if (it != inventories.end())
        {
            it->second->SetStateId(state_id);
        }
    }
#endif

    void InventoryManager::AddInventory(const short window_id, const InventoryType window_type)
    {
        std::scoped_lock<std::shared_mutex> lock(inventory_manager_mutex);
        inventories[window_id] = std::make_shared<Window>(window_type);
#if PROTOCOL_VERSION < 755 /* < 1.17 */
        pending_transactions[window_id] = std::map<short, InventoryTransaction >();
        transaction_states[window_id] = std::map<short, TransactionState>();
#endif
    }

    void InventoryManager::SetHotbarSelected(const short index)
    {
        std::scoped_lock<std::shared_mutex> lock(inventory_manager_mutex);
        index_hotbar_selected = index;
    }

    Slot InventoryManager::GetCursor() const
    {
        std::shared_lock<std::shared_mutex> lock(inventory_manager_mutex);
        return cursor;
    }

    void InventoryManager::SetCursor(const Slot& c)
    {
        std::scoped_lock<std::shared_mutex> lock(inventory_manager_mutex);
        cursor = c;
    }

    InventoryTransaction InventoryManager::PrepareTransaction(const std::shared_ptr<ServerboundContainerClickPacket>& transaction)
    {
        // Get the container
        std::shared_ptr<Window> window = GetWindow(transaction->GetContainerId());

        // Lock because we need read access to cursor
        std::shared_lock<std::shared_mutex> lock(inventory_manager_mutex);

        InventoryTransaction output{ transaction };
        std::map<short, Slot> changed_slots;
        Slot carried_item;

        // Process the transaction

        // Click on the output of a crafting container
        if ((window->GetType() == InventoryType::PlayerInventory || window->GetType() == InventoryType::Crafting) &&
            transaction->GetSlotNum() == 0)
        {
            if (transaction->GetClickType() != 0 && transaction->GetClickType() != 1)
            {
                LOG_ERROR("Transaction type '" << transaction->GetClickType() << "' not implemented.");
                throw std::runtime_error("Non supported transaction type created");
            }

            if (transaction->GetButtonNum() != 0 && transaction->GetButtonNum() != 1)
            {
                LOG_ERROR("Transaction button num '" << transaction->GetButtonNum() << "' not supported.");
                throw std::runtime_error("Non supported transaction button created");
            }

            const Slot clicked_slot = window->GetSlot(transaction->GetSlotNum());
            // If cursor is not empty, we can't click if the items are not the same,
            if (!cursor.IsEmptySlot() && !cursor.SameItem(clicked_slot))
            {
                carried_item = cursor;
            }
            // We can't click if the crafted items don't fit in the stack
            else if (!cursor.IsEmptySlot() && (cursor.GetItemCount() + clicked_slot.GetItemCount()) > AssetsManager::getInstance().Items().at(cursor.GetItemId())->GetStackSize())
            {
                carried_item = cursor;
            }
            else
            {
                carried_item = clicked_slot;
                carried_item.SetItemCount(cursor.GetItemCount() + clicked_slot.GetItemCount());
                changed_slots[0] = Slot();
                for (int i = 1; i < (window->GetType() == InventoryType::Crafting ? 10 : 5); ++i)
                {
                    Slot cloned_slot = window->GetSlot(i);
                    cloned_slot.SetItemCount(cloned_slot.GetItemCount() - 1);
                    changed_slots[i] = cloned_slot;
                }
            }
        }
        // Drop item
        else if (transaction->GetSlotNum() == -999)
        {
            switch (transaction->GetClickType())
            {
                case 0:
                {
                    switch (transaction->GetButtonNum())
                    {
                        case 0:
                        {
                            carried_item = Slot();
                            break;
                        }
                        case 1:
                        {
                            carried_item = cursor;
                            carried_item.SetItemCount(cursor.GetItemCount() - 1);
                            break;
                        }
                        default:
                        {
                            break;
                        }
                    }
                    break;
                }
                default:
                {
                    LOG_ERROR("Transaction type '" << transaction->GetClickType() << "' not implemented.");
                    throw std::runtime_error("Non supported transaction type created");
                    break;
                }
            }
        }
        // Normal case
        else
        {
            switch (transaction->GetClickType())
            {
                case 0:
                {
                    const Slot clicked_slot = window->GetSlot(transaction->GetSlotNum());
                    switch (transaction->GetButtonNum())
                    {

                        case 0: // "Left click"
                        {
                            // Special case: left click with same item
                            if (!cursor.IsEmptySlot() && !clicked_slot.IsEmptySlot() && cursor.SameItem(clicked_slot))
                            {
                                const int sum_count = cursor.GetItemCount() + clicked_slot.GetItemCount();
                                const int max_stack_size = AssetsManager::getInstance().Items().at(cursor.GetItemId())->GetStackSize();
                                // The cursor becomes the clicked slot
                                carried_item = clicked_slot;
                                carried_item.SetItemCount(std::max(0, sum_count - max_stack_size));
                                // The content of the clicked slot becomes the cursor
                                changed_slots = { {transaction->GetSlotNum(), cursor} };
                                changed_slots.at(transaction->GetSlotNum()).SetItemCount(std::min(max_stack_size, sum_count));
                            }
                            else
                            {
                                // The cursor becomes the clicked slot
                                carried_item = clicked_slot;
                                // The content of the clicked slot becomes the cursor
                                changed_slots = { {transaction->GetSlotNum(), cursor} };
                            }
                            break;
                        }
                        case 1: // "Right Click"
                        {
                            // If cursor is empty, take half the stack
                            if (cursor.IsEmptySlot())
                            {
                                const int new_quantity = clicked_slot.GetItemCount() / 2;
                                carried_item = clicked_slot;
                                carried_item.SetItemCount(clicked_slot.GetItemCount() - new_quantity);
                                changed_slots = { {transaction->GetSlotNum(), clicked_slot} };
                                changed_slots.at(transaction->GetSlotNum()).SetItemCount(new_quantity);
                            }
                            // If clicked is empty, put one item in the slot
                            else if (clicked_slot.IsEmptySlot())
                            {
                                // Cursor is the same, minus one item
                                carried_item = cursor;
                                carried_item.SetItemCount(cursor.GetItemCount() - 1);
                                // Dest slot it the same, plus one item
                                changed_slots = { {transaction->GetSlotNum(), cursor} };
                                changed_slots.at(transaction->GetSlotNum()).SetItemCount(1);
                            }
                            // If same items in both
                            else if (cursor.SameItem(clicked_slot))
                            {
                                const int max_stack_size = AssetsManager::getInstance().Items().at(cursor.GetItemId())->GetStackSize();
                                const bool transfer = clicked_slot.GetItemCount() < max_stack_size;
                                // The cursor loses 1 item if possible
                                carried_item = clicked_slot;
                                carried_item.SetItemCount(transfer ? cursor.GetItemCount() - 1 : cursor.GetItemCount());
                                // The content of the clicked slot gains one item if possible
                                changed_slots = { {transaction->GetSlotNum(), cursor} };
                                changed_slots.at(transaction->GetSlotNum()).SetItemCount(transfer ? clicked_slot.GetItemCount() + 1 : clicked_slot.GetItemCount());
                            }
                            // Else just switch like a left click
                            else
                            {
                                // The cursor becomes the clicked slot
                                carried_item = clicked_slot;
                                // The content of the clicked slot becomes the cursor
                                changed_slots = { {transaction->GetSlotNum(), cursor} };
                            }
                            break;
                        }
                        default:
                            break;
                    }
                    break;
                }
                default:
                {
                    LOG_ERROR("Transaction type '" << transaction->GetClickType() << "' not implemented.");
                    throw std::runtime_error("Non supported transaction type created");
                    break;
                }
            }
        }

#if PROTOCOL_VERSION < 755 /* < 1.17 */
        transaction->SetCarriedItem(window->GetSlot(transaction->GetSlotNum()));
        output.changed_slots = changed_slots;
        output.carried_item = carried_item;

        transaction->SetUid(window->GetNextTransactionId());
#elif PROTOCOL_VERSION < 770 /* < 1.21.5 */
        transaction->SetCarriedItem(carried_item);
        transaction->SetChangedSlots(changed_slots);
#if PROTOCOL_VERSION > 755 /* > 1.17 */
        transaction->SetStateId(window->GetStateId());
#endif
#else
        transaction->SetCarriedItem(static_cast<HashedSlot>(carried_item));
        std::map<short, HashedSlot> hashed_changed_slots;
        for (const auto& [k, v] : changed_slots)
        {
            hashed_changed_slots[k] = static_cast<HashedSlot>(v);
        }
        transaction->SetChangedSlots(hashed_changed_slots);
        transaction->SetStateId(window->GetStateId());
        // Store the real slots with the non-hashed components
        output.changed_slots = changed_slots;
        output.carried_item = carried_item;
#endif
        return output;
    }

    void InventoryManager::ApplyTransactionImpl(const InventoryTransaction& transaction)
    {
#if PROTOCOL_VERSION < 755 /* < 1.17 */ || PROTOCOL_VERSION > 769 /* > 1.21.4 */
        const std::map<short, Slot>& modified_slots = transaction.changed_slots;
        cursor = transaction.carried_item;
#else
        const std::map<short, Slot>& modified_slots = transaction.packet->GetChangedSlots();
        cursor = transaction.packet->GetCarriedItem();
#endif

        // Get the container
        std::shared_ptr<Window> window = inventories[transaction.packet->GetContainerId()];
        for (const auto& p : modified_slots)
        {
            window->SetSlot(p.first, p.second);
        }
    }

    void InventoryManager::ApplyTransaction(const InventoryTransaction& transaction)
    {
        std::scoped_lock<std::shared_mutex> lock(inventory_manager_mutex);
        ApplyTransactionImpl(transaction);
    }

#if PROTOCOL_VERSION > 451 /* > 1.13.2 */
    std::vector<MerchantOffer> InventoryManager::GetAvailableMerchantOffers() const
    {
        std::shared_lock<std::shared_mutex> lock(inventory_manager_mutex);
        return available_trades;
    }

    void InventoryManager::IncrementMerchantOfferUse(const int index)
    {
        std::scoped_lock<std::shared_mutex> lock(inventory_manager_mutex);
        if (index < 0 || index > available_trades.size())
        {
            LOG_WARNING("Trying to update trade use of an invalid trade (" << index << "<" << available_trades.size() << ")");
            return;
        }
        MerchantOffer& trade = available_trades[index];
        trade.SetNumberOfTradesUses(trade.GetNumberOfTradesUses() + 1);
    }
#endif


    void InventoryManager::Handle(ClientboundContainerSetSlotPacket& packet)
    {
        if (packet.GetContainerId() == -1 && packet.GetSlot() == -1)
        {
            SetCursor(packet.GetItemStack());
        }
        // Slot index is starting in the hotbar, THEN in the main storage in this case :)
        else if (packet.GetContainerId() == -2)
        {
            if (packet.GetSlot() < Window::INVENTORY_OFFHAND_INDEX - Window::INVENTORY_HOTBAR_START)
            {
                SetSlot(Window::PLAYER_INVENTORY_INDEX, Window::INVENTORY_HOTBAR_START + packet.GetSlot(), packet.GetItemStack());
            }
            else
            {
                SetSlot(Window::PLAYER_INVENTORY_INDEX, packet.GetSlot() + Window::INVENTORY_OFFHAND_INDEX - Window::INVENTORY_HOTBAR_START, packet.GetItemStack());
            }
        }
        else if (packet.GetContainerId() >= 0)
        {
            SetSlot(packet.GetContainerId(), packet.GetSlot(), packet.GetItemStack());
#if PROTOCOL_VERSION > 755 /* > 1.17 */
            SetStateId(packet.GetContainerId(), packet.GetStateId());
#endif
        }
        else
        {
            LOG_WARNING("Unknown window called during ClientboundContainerSetSlotPacket Handle : " << packet.GetContainerId() << ", " << packet.GetSlot());
        }
    }

    void InventoryManager::Handle(ClientboundContainerSetContentPacket& packet)
    {
        std::shared_ptr<Window> window = GetWindow(packet.GetContainerId());
        if (window != nullptr)
        {
            window->SetContent(packet.GetItems());
        }

#if PROTOCOL_VERSION > 755 /* > 1.17 */
        if (packet.GetContainerId() >= 0)
        {
            SetStateId(packet.GetContainerId(), packet.GetStateId());
        }
#endif
    }

    void InventoryManager::Handle(ClientboundOpenScreenPacket& packet)
    {
#if PROTOCOL_VERSION < 452 /* < 1.14 */
        InventoryType type = InventoryType::Default;
        if (packet.GetType() == "minecraft:chest")
        {
            switch (packet.GetNumberOfSlots())
            {
            case 9*3:
                type = InventoryType::Generic9x3;
                break;
            case 9*6:
                type = InventoryType::Generic9x6;
                break;
            default:
                LOG_ERROR("Not implemented chest type : " << packet.GetType());
                break;
            }
        }
        else if (packet.GetType() == "minecraft:crafting_table")
        {
            type = InventoryType::Crafting;
        }
        else
        {
            LOG_ERROR("Not implemented container type : " << packet.GetType());
        }
        AddInventory(packet.GetContainerId(), type);
#else
        AddInventory(packet.GetContainerId(), static_cast<InventoryType>(packet.GetType()));
#endif
    }

#if PROTOCOL_VERSION < 768 /* < 1.21.2 */
    void InventoryManager::Handle(ClientboundSetCarriedItemPacket& packet)
#else
    void InventoryManager::Handle(ClientboundSetHeldSlotPacket& packet)
#endif
    {
        SetHotbarSelected(packet.GetSlot());
    }

#if PROTOCOL_VERSION < 755 /* < 1.17 */
    void InventoryManager::Handle(ClientboundContainerAckPacket& packet)
    {
        std::scoped_lock<std::shared_mutex> lock(inventory_manager_mutex);

        // Update the new state of the transaction
        auto it_container = transaction_states.find(packet.GetContainerId());
        if (it_container == transaction_states.end())
        {
            transaction_states[packet.GetContainerId()] = std::map<short, TransactionState>();
            it_container = transaction_states.find(packet.GetContainerId());
        }
        it_container->second[packet.GetUid()] = packet.GetAccepted() ? TransactionState::Accepted : TransactionState::Refused;

        auto container_transactions = pending_transactions.find(packet.GetContainerId());

        if (container_transactions == pending_transactions.end())
        {
            LOG_WARNING("The server accepted a transaction for an unknown container");
            return;
        }

        auto transaction = container_transactions->second.find(packet.GetUid());

        // Get the corresponding transaction
        if (transaction == container_transactions->second.end())
        {
            LOG_WARNING("Server accepted an unknown transaction Uid");
            return;
        }

        if (packet.GetAccepted())
        {
            ApplyTransactionImpl(transaction->second);
        }

        // Remove the transaction from the waiting state
        container_transactions->second.erase(transaction);
    }
#endif

#if PROTOCOL_VERSION > 451 /* > 1.13.2 */
    void InventoryManager::Handle(ClientboundMerchantOffersPacket& packet)
    {
        std::scoped_lock<std::shared_mutex> lock(inventory_manager_mutex);
        trading_container_id = packet.GetContainerId();
        available_trades = packet.GetOffers();
    }
#endif

    void InventoryManager::Handle(ClientboundContainerClosePacket& packet)
    {
        EraseInventory(static_cast<short>(packet.GetContainerId()));
    }

#if PROTOCOL_VERSION > 767 /* > 1.21.1 */
    void InventoryManager::Handle(ClientboundSetCursorItemPacket& packet)
    {
        // Not sure about this one, I can't figure out when it's sent by the server
        SetSlot(Window::PLAYER_INVENTORY_INDEX, Window::INVENTORY_HOTBAR_START + index_hotbar_selected, packet.GetContents());
    }

    void InventoryManager::Handle(ProtocolCraft::ClientboundSetPlayerInventoryPacket& packet)
    {
        SetSlot(Window::PLAYER_INVENTORY_INDEX, static_cast<short>(packet.GetSlot()), packet.GetContents());
    }
#endif

} //Botcraft
