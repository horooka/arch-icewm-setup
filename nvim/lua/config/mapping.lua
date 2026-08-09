local k = vim.keymap

-- {"n", "v"}, "p", "paste to a next line"
-- {"n", "v"}, "s", "jump to a entered letter"

--neovide
if vim.g.neovide then
	vim.g.neovide_scale_factor = 1.0
	local change_scale_factor = function(delta)
		vim.g.neovide_scale_factor = vim.g.neovide_scale_factor * delta
	end
	k.set("n", "<C-=>", function()
		change_scale_factor(1.25)
	end)
	k.set("n", "<C-->", function()
		change_scale_factor(0.8)
	end)
	k.set("n", "<C-0>", function()
		vim.g.neovide_scale_factor = 1.0
	end)
end

--file
k.set({ "n", "i" }, ",q", "<Esc>:q!<CR>", { noremap = true, silent = true })
k.set({ "n", "i" }, ",e", "<Esc>:wq<CR>", { noremap = true, silent = true })
k.set({ "n", "i" }, ",w", "<Esc>:w<CR>", { noremap = true, silent = true })
k.set({ "n", "i", "v" }, ",s", "<Esc>", { noremap = true, silent = true })
k.set({ "n" }, "<space>m", "<Esc>:make<CR>", { noremap = true, silent = true })
k.set({ "n" }, "<space>c", "<Esc>:copen<CR>", { noremap = true, silent = true })
--compilation
vim.api.nvim_create_autocmd({ "BufEnter", "BufWinEnter" }, {
	callback = function()
		local ft = vim.bo.filetype
		if ft == "rust" then
			vim.bo.makeprg = "cargo check"
		elseif ft == "c" or ft == "cpp" then
			vim.bo.makeprg = "cmake --build build -j $(nproc)"
		else
			vim.bo.makeprg = ""
		end
	end,
})

-- modes
k.set({ "n", "i" }, ",v", "<Esc>v", { noremap = true, silent = true })
k.set({ "n", "i" }, ",t", "<Esc>:!", { noremap = true, silent = true })
k.set({ "n", "i" }, ",r", "<Esc>:%s/", { noremap = true, silent = true })
k.set("v", ",r", "<Esc>:'<,'>s/", { noremap = true, silent = false })

-- lines
k.set({ "n", "i" }, ",b", "<Esc>^i", { noremap = true, silent = true })
k.set({ "n", "i" }, ",a", "<Esc>A", { noremap = true, silent = true })
k.set({ "n", "i" }, ",d", "<Esc>Vx", { noremap = true, silent = true })
k.set({ "n", "i" }, ",c", "<Esc>Vy", { noremap = true, silent = true })

-- tags
k.set("n", "<space>d", "<C-]>", { desc = "Tag: go to definition" })
k.set("n", "<space>q", "<C-t>", { desc = "Tag: back" })
-- Shift+k - help for object under cursor
-- k.set("n", "<space>t", "<C-w>t", { desc = "Tag: open tag file" })
-- k.set("n", "<space>s", ":tnext<CR>", { desc = "Tag: next match" })
-- k.set("n", "<space>a", ":tprev<CR>", { desc = "Tag: prev match" })
