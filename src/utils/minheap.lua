local function createMinHeap()
    local heap = { items = {} }

    local function swap(i, j)
        local tmp = heap.items[i]
        heap.items[i] = heap.items[j]
        heap.items[j] = tmp
    end

    local function siftUp(idx)
        while idx > 1 do
            local parent = math.floor(idx / 2)
            if heap.items[idx].priority < heap.items[parent].priority then
                swap(idx, parent)
                idx = parent
            else
                break
            end
        end
    end

    local function siftDown(idx)
        local size = #heap.items
        while true do
            local smallest = idx
            local left = 2 * idx
            local right = 2 * idx + 1

            if left <= size and heap.items[left].priority < heap.items[smallest].priority then
                smallest = left
            end
            if right <= size and heap.items[right].priority < heap.items[smallest].priority then
                smallest = right
            end

            if smallest ~= idx then
                swap(idx, smallest)
                idx = smallest
            else
                break
            end
        end
    end

    heap.push = function(self, value, priority)
        table.insert(self.items, { value = value, priority = priority })
        siftUp(#self.items)
    end

    heap.pop = function(self)
        if #self.items == 0 then return nil end
        local result = self.items[1]
        local last = table.remove(self.items)
        if #self.items > 0 then
            self.items[1] = last
            siftDown(1)
        end
        return result.value
    end

    heap.isEmpty = function(self)
        return #self.items == 0
    end

    heap.clear = function(self)
        self.items = {}
    end

    return heap
end

return { createMinHeap = createMinHeap }