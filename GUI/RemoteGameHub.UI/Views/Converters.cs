using System;
using System.Globalization;
using System.IO;
using System.Windows;
using System.Windows.Data;
using System.Windows.Media;
using RemoteGameHub.UI.Models;
using RemoteGameHub.UI.ViewModels;

namespace RemoteGameHub.UI.Views
{
    /// <summary>兼容性等级 -> 颜色画刷</summary>
    public class CompatibilityToBrushConverter : IValueConverter
    {
        public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        {
            if (value is GameCompatibility c)
            {
                return c switch
                {
                    GameCompatibility.Perfect => new SolidColorBrush(Color.FromRgb(0x4c, 0xaf, 0x50)),
                    GameCompatibility.Experimental => new SolidColorBrush(Color.FromRgb(0xff, 0x98, 0x00)),
                    GameCompatibility.Incompatible => new SolidColorBrush(Color.FromRgb(0xf4, 0x43, 0x36)),
                    _ => new SolidColorBrush(Color.FromRgb(0xaa, 0xaa, 0xaa))
                };
            }
            return new SolidColorBrush(Color.FromRgb(0xaa, 0xaa, 0xaa));
        }

        public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
            => Binding.DoNothing;
    }

    /// <summary>封面路径 -> 图像源（路径无效时返回 UnsetValue，由 XAML 显示背景色）</summary>
    public class CoverPathConverter : IValueConverter
    {
        public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        {
            if (value is string path && !string.IsNullOrWhiteSpace(path))
            {
                try
                {
                    if (File.Exists(path))
                        return path;
                    // 尝试作为相对路径或 URI 解析
                    if (Uri.TryCreate(path, UriKind.RelativeOrAbsolute, out _))
                        return path;
                }
                catch { }
            }
            // 路径无效时返回 UnsetValue，XAML 将显示背景色作为占位
            return DependencyProperty.UnsetValue;
        }

        public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
            => Binding.DoNothing;
    }

    /// <summary>空字符串 -> 可见（用于搜索框水印）</summary>
    public class EmptyToVisibleConverter : IValueConverter
    {
        public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        {
            bool empty = string.IsNullOrEmpty(value as string);
            return empty ? Visibility.Visible : Visibility.Collapsed;
        }

        public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
            => Binding.DoNothing;
    }

    /// <summary>布尔 -> 可见性</summary>
    public class BoolToVisibleConverter : IValueConverter
    {
        public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        {
            return (value is bool b && b) ? Visibility.Visible : Visibility.Collapsed;
        }

        public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
            => Binding.DoNothing;
    }

    /// <summary>导航枚举 -> RadioButton 是否选中</summary>
    public class NavToBoolConverter : IValueConverter
    {
        public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        {
            if (value is NavCategory current && parameter is string s
                && Enum.TryParse<NavCategory>(s, out var target))
            {
                return current == target;
            }
            return false;
        }

        public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        {
            // 单向使用，ConvertBack 不需要实现
            return Binding.DoNothing;
        }
    }
}
