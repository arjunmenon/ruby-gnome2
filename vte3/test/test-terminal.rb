# Copyright (C) 2014-2015  Ruby-GNOME2 Project Team
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

class TestTerminal < Test::Unit::TestCase
  include VteTestUtils

  def setup
    @terminal = Vte::Terminal.new
  end

  def test_font
    font = Pango::FontDescription.new("Monospace 16")
    @terminal.font = font
    assert_equal(font, @terminal.font)
  end

  sub_test_case "#spawn" do
    teardown do
      loop = GLib::MainLoop.new
      GLib::Idle.add do
        loop.quit
        GLib::Source::REMOVE
      end
      loop.run
    end

    test "success" do
      pid = @terminal.spawn(:argv => ["echo"])
      assert do
        pid > 0
      end
    end

    test "failure" do
      assert_raise(GLib::SpawnError) do
        @terminal.spawn(:argv => ["nonexistent"])
      end
    end
  end
end
